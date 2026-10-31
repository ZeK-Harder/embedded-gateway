#define _POSIX_C_SOURCE 200809L   /* clock_gettime：批量提交那 50ms 的时间基准 */
/* store：SQLite 落库层。
 * store_open / store_close 是样板，学习点在中间那个 store_init_schema。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>      /* memset */
#include <time.h>        /* clock_gettime */
#include <pthread.h>     /* 一个 db 句柄被 4 个 worker 共用 */

#include "store.h"

int store_open(const char *path, sqlite3 **out)
{
    sqlite3 *db = NULL;
    int rc;

    if (path == NULL || out == NULL) {
        fprintf(stderr, "store_open: 参数为空\n");
        return -1;
    }

    /* sqlite3_open: 文件不存在就新建（不是错误），返回 SQLITE_OK 才算成功 */
    rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        /* 失败时 db 可能仍然非空（用来取错误信息），必须关掉，否则泄漏 */
        fprintf(stderr, "store_open(%s): rc=%d %s\n", path, rc, sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    *out = db;
    return 0;
}

void store_close(sqlite3 *db)
{
    if (db != NULL) {
        sqlite3_close(db);
    }
}

int store_init_schema(sqlite3 *db)
{
    int rc;

    /* 表 1：设备表 —— 一台设备一行，dev_id 是主键（0~255，来自帧头） */
    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS devices ("
        "    dev_id    INTEGER PRIMARY KEY,"
        "    last_seen INTEGER NOT NULL"       /* 最后一次上报的 unix 秒 */
        ")", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "store_init_schema devices: rc=%d %s\n", rc, sqlite3_errmsg(db));
        return -1;
    }

    /* 表 2：事件表 —— 每帧一行；id 自增，主键不是 dev_id（同一设备有很多帧） */
    rc = sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS events ("
        "    id     INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    dev_id INTEGER NOT NULL CHECK (dev_id BETWEEN 0 AND 255),"
        "    ts     INTEGER NOT NULL,"
        "    func   INTEGER NOT NULL,"
        "    val    INTEGER"
        ")", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "store_init_schema events: rc=%d %s\n", rc, sqlite3_errmsg(db));
        return -1;
    }

    /* 索引：让"某设备最近 N 条"（WHERE dev_id=? ORDER BY ts DESC）走索引，不全表扫 */
    rc = sqlite3_exec(db,
        "CREATE INDEX IF NOT EXISTS idx_events_dev_ts ON events (dev_id, ts)",
        NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "store_init_schema index: rc=%d %s\n", rc, sqlite3_errmsg(db));
        return -1;
    }

    return 0;
}

int store_insert_event(sqlite3 *db, int dev_id, long long ts, int func, int val)
{
    char sql[256];
    int rc;

    snprintf(sql, sizeof sql,
             "INSERT INTO events (dev_id, ts, func, val) VALUES (%d, %lld, %d, %d);",
             dev_id, ts, func, val);

    rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    if (rc == SQLITE_OK) {
        return 0;
    }

    if(rc == SQLITE_CONSTRAINT){
        fprintf(stderr, "store_insert_event: rc=%d %s\n", rc, sqlite3_errmsg(db));
        return -1;
    }
    if(rc == SQLITE_BUSY){
        fprintf(stderr, "store_insert_event: rc=%d %s\n", rc, sqlite3_errmsg(db));
        return -2;
    }
    else{
        fprintf(stderr, "store_insert_event: rc=%d %s\n", rc, sqlite3_errmsg(db));
        return -3;
    }
}

/* 为什么不能把 cb 直接交给 exec：exec 的回调签名要的是 char**（字符串），
 * 而 store_row_cb 要的是 int/long long；exec 又只有一个 void* 能带上下文，
 * 所以用这个 struct 把 cb 和 ctx 一起递进去，由 row_adapter 负责转换。
 */
struct qctx {
    store_row_cb cb;
    void *ctx;
};

static int row_adapter(void *arg, int ncol, char **vals, char **names)
{
    struct qctx *q = arg;

    (void)names;
    if (ncol < 4) {
        return 1;       /* 列数不对就别往下走，让 exec 直接中止 */
    }

    /* 字符串 -> 数字，然后交回调用方（列顺序来自 SELECT 的列顺序） */
    q->cb(q->ctx, atoi(vals[0]), atoll(vals[1]), atoi(vals[2]), atoi(vals[3]));
    return 0;
}

int store_query_recent(sqlite3 *db, int dev_id, int limit, store_row_cb cb, void *ctx)
{
    char sql[256];
    struct qctx q;
    int rc;

    q.cb = cb;
    q.ctx = ctx;

    snprintf(sql, sizeof sql,
             "SELECT dev_id, ts, func, val FROM events WHERE dev_id = %d ORDER BY ts DESC LIMIT %d;",
             dev_id, limit);

    rc = sqlite3_exec(db, sql, row_adapter, &q, NULL);

    if (rc == SQLITE_OK) {
        return 0;
    }
    if (rc == SQLITE_BUSY) {
        fprintf(stderr, "store_query_recent: rc=%d %s\n", rc, sqlite3_errmsg(db));
        return -2;
    }
    fprintf(stderr, "store_query_recent: rc=%d %s\n", rc, sqlite3_errmsg(db));
    return -3;
}

/* ===== 批量事务实现（2026-10-31 学生选 B）===== */

static long long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

int store_batch_init(store_batch_t *b, sqlite3 *db)
{
    if (b == NULL || db == NULL) {
        fprintf(stderr, "store_batch_init: 参数为空\n");
        return -1;
    }
    memset(b, 0, sizeof *b);
    b->db = db;
    if (pthread_mutex_init(&b->mu, NULL) != 0) {
        fprintf(stderr, "store_batch_init: 互斥锁初始化失败\n");
        return -1;
    }
    b->last_commit_ms = now_ms();
    return 0;
}

/* 调用方必须已持锁 */
static int batch_commit_locked(store_batch_t *b)
{
    int rc;

    if (!b->in_tx) return 0;

    rc = sqlite3_exec(b->db, "COMMIT", NULL, NULL, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "store: COMMIT 失败 rc=%d %s\n", rc, sqlite3_errmsg(b->db));
        /* 别把坏事务挂在连接上：后面每一句 INSERT 都会跟着失败 */
        sqlite3_exec(b->db, "ROLLBACK", NULL, NULL, NULL);
        b->in_tx = 0;
        b->pending = 0;
        b->last_commit_ms = now_ms();
        return (rc == SQLITE_BUSY) ? -2 : -3;
    }
    b->in_tx = 0;
    b->pending = 0;
    b->last_commit_ms = now_ms();
    return 0;
}

int store_batch_flush(store_batch_t *b)
{
    int rc;

    if (b == NULL) return -1;
    pthread_mutex_lock(&b->mu);
    rc = batch_commit_locked(b);
    pthread_mutex_unlock(&b->mu);
    return rc;
}

void store_batch_close(store_batch_t *b)
{
    if (b == NULL) return;
    (void)store_batch_flush(b);
    pthread_mutex_destroy(&b->mu);
}

int store_insert_event_batched(store_batch_t *b, int dev_id, long long ts, int func, int val)
{
    int ret;
    int out;

    if (b == NULL || b->db == NULL) return -3;

    pthread_mutex_lock(&b->mu);

    if (!b->in_tx) {
        int rc = sqlite3_exec(b->db, "BEGIN", NULL, NULL, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "store: BEGIN 失败 rc=%d %s\n", rc, sqlite3_errmsg(b->db));
            pthread_mutex_unlock(&b->mu);
            return (rc == SQLITE_BUSY) ? -2 : -3;
        }
        b->in_tx   = 1;
        b->pending = 0;
    }

    ret = store_insert_event(b->db, dev_id, ts, func, val);
    if (ret == 0) b->pending++;
    out = ret;

    /* 够一批（条数或时间）就提交：时间条件管住"低负载时数据别在内存里躺太久" */
    if (b->pending >= STORE_BATCH_MAX || (now_ms() - b->last_commit_ms) >= STORE_BATCH_MS) {
        int crc = batch_commit_locked(b);
        if (crc != 0 && out == 0) out = crc;
    }

    pthread_mutex_unlock(&b->mu);
    return out;
}

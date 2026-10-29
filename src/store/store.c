/* store：SQLite 落库层。
 * store_open / store_close 是样板，学习点在中间那个 store_init_schema。
 */
#include <stdio.h>
#include <stdlib.h>

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

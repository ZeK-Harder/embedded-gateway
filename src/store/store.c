/* store：SQLite 落库层。
 * store_open / store_close 是样板，学习点在中间那个 store_init_schema。
 */
#include <stdio.h>

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

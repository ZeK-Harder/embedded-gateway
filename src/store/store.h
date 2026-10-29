#ifndef STORE_H
#define STORE_H

#include <sqlite3.h>

/* 打开（或新建）一个库文件。成功 rc=0 且 *out 可用；失败 -1 且 *out 不变。 */
int store_open(const char *path, sqlite3 **out);

/* 关库。传 NULL 直接返回，不崩。 */
void store_close(sqlite3 *db);

/* 建表（幂等：已存在就当成功）。成功 0，失败 -1。 */
int store_init_schema(sqlite3 *db);

/* 插一条事件。返回值（调用方靠它决定"重试 / 丢弃 / 报警"，所以必须区分）：
 *   0  = 写进库了
 *  -1  = 数据本身不合规（约束冲突等）→ 调用方丢掉这条帧，继续跑
 *  -2  = 库暂时被锁（SQLITE_BUSY）→ 调用方稍后重试；本函数不自己重试
 *  -3  = 库写不进去（磁盘满 / IO 错 / 库损坏）→ 调用方报警并停止落库
 */
int store_insert_event(sqlite3 *db, int dev_id, long long ts, int func, int val);

/* 每查出一行，store 层就回调一次，把该行交给你（打印 / 塞缓冲 / 丢掉都行）。 */
typedef void (*store_row_cb)(void *ctx, int dev_id, long long ts, int func, int val);

/* 查某台设备最近 limit 条事件，按 ts 从新到旧。
 * 返回值与上面一致：0 成功 / -1 SQL 错 / -2 BUSY / -3 其他。
 * cb 传 NULL 时只数行不回调（后面要"查最近 N 条"的计数也用得上）。 */
int store_query_recent(sqlite3 *db, int dev_id, int limit, store_row_cb cb, void *ctx);

#endif

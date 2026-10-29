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

#endif

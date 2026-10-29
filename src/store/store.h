#ifndef STORE_H
#define STORE_H

#include <sqlite3.h>

/* 打开（或新建）一个库文件。成功 rc=0 且 *out 可用；失败 -1 且 *out 不变。 */
int store_open(const char *path, sqlite3 **out);

/* 关库。传 NULL 直接返回，不崩。 */
void store_close(sqlite3 *db);

/* 建表（幂等：已存在就当成功）。成功 0，失败 -1。 */
int store_init_schema(sqlite3 *db);

#endif

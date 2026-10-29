/* store 探针：只负责"按顺序调函数并把结果喊出来"，不含学习点。
 * 用法：
 *   ./bin/store_probe <库路径>                                  仅建表
 *   ./bin/store_probe <库路径> insert <dev> <ts> <func> <val>    插一条（表要已建好）
 *   ./bin/store_probe <库路径> seed                             建表 + 灌 18 条固定数据
 *   ./bin/store_probe <库路径> query <dev> <N>                  查最近 N 条并逐行打印
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"

static void print_row(void *ctx, int dev_id, long long ts, int func, int val)
{
    int *n = ctx;
    (*n)++;
    printf("row#%d dev=%d ts=%lld func=%d val=%d\n", *n, dev_id, ts, func, val);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/tmp/gateway.db";
    const char *mode = (argc > 2) ? argv[2] : "";
    sqlite3 *db = NULL;
    int rc;

    if (store_open(path, &db) != 0) {
        fprintf(stderr, "PROBE FAIL: store_open(%s)\n", path);
        return 1;
    }
    printf("db=%s opened\n", path);

    if (strcmp(mode, "insert") == 0 || strcmp(mode, "query") == 0) {
        /* 这两种模式库和表已经由前面的步骤建好了，不再建表（否则"库被锁"压不到 insert 上） */
    } else {
        if (store_init_schema(db) != 0) {
            fprintf(stderr, "PROBE FAIL: store_init_schema\n");
            store_close(db);
            return 1;
        }
        printf("schema ok\n");
    }

    if (strcmp(mode, "insert") == 0) {
        if (argc < 7) {
            fprintf(stderr, "用法: %s <db> insert <dev> <ts> <func> <val>\n", argv[0]);
            store_close(db);
            return 1;
        }
        rc = store_insert_event(db, atoi(argv[3]), atoll(argv[4]), atoi(argv[5]), atoi(argv[6]));
        printf("insert_rc=%d\n", rc);
        store_close(db);
        return (rc == 0) ? 0 : 2;
    }

    if (strcmp(mode, "seed") == 0) {
        /* dev=5 灌 15 条（ts 1000..1014）、dev=7 灌 3 条（ts 2000..2002） */
        for (int i = 0; i < 15; i++) {
            rc = store_insert_event(db, 5, 1000 + i, 1, i);
            if (rc != 0) {
                fprintf(stderr, "seed dev=5 第 %d 条失败 rc=%d\n", i, rc);
                store_close(db);
                return 1;
            }
        }
        for (int i = 0; i < 3; i++) {
            rc = store_insert_event(db, 7, 2000 + i, 2, 100 + i);
            if (rc != 0) {
                fprintf(stderr, "seed dev=7 第 %d 条失败 rc=%d\n", i, rc);
                store_close(db);
                return 1;
            }
        }
        printf("seeded 18 rows\n");
        store_close(db);
        return 0;
    }

    if (strcmp(mode, "query") == 0) {
        if (argc < 5) {
            fprintf(stderr, "用法: %s <db> query <dev> <N>\n", argv[0]);
            store_close(db);
            return 1;
        }
        int printed = 0;
        rc = store_query_recent(db, atoi(argv[3]), atoi(argv[4]), print_row, &printed);
        printf("query_rc=%d rows=%d\n", rc, printed);
        store_close(db);
        return (rc == 0) ? 0 : 2;
    }

    store_close(db);
    printf("closed\n");
    return 0;
}

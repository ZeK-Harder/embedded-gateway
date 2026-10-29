/* store 探针：只负责"按顺序调函数并把结果喊出来"，不含学习点。
 * 用法：
 *   ./bin/store_probe <库路径>                                  仅建表
 *   ./bin/store_probe <库路径> insert <dev> <ts> <func> <val>    建表 + 插一条
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "store.h"

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/tmp/gateway.db";
    int mode_insert = (argc > 2 && strcmp(argv[2], "insert") == 0);
    sqlite3 *db = NULL;

    if (store_open(path, &db) != 0) {
        fprintf(stderr, "PROBE FAIL: store_open(%s)\n", path);
        return 1;
    }
    printf("db=%s opened\n", path);

    /* insert 模式不再建表：库已经由脚本先建好
     * 这样"库被别的连接锁住"的场景才能真的压到 insert 那句上，而不是先卡在建表。 */
    if (!mode_insert) {
        if (store_init_schema(db) != 0) {
            fprintf(stderr, "PROBE FAIL: store_init_schema\n");
            store_close(db);
            return 1;
        }
        printf("schema ok\n");
        store_close(db);
        printf("closed\n");
        return 0;
    }

    if (argc < 7) {
        fprintf(stderr, "用法: %s <db> insert <dev> <ts> <func> <val>\n", argv[0]);
        store_close(db);
        return 1;
    }

    int rc = store_insert_event(db, atoi(argv[3]), atoll(argv[4]), atoi(argv[5]), atoi(argv[6]));
    printf("insert_rc=%d\n", rc);
    store_close(db);
    return (rc == 0) ? 0 : 2;
}

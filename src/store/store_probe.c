/* store 探针：只负责"按顺序调三个函数并把结果喊出来"。
 * 真正的学点在 store.c 的 store_init_schema（导师留的空洞）。
 * 用法：./bin/store_probe [库文件路径]
 */
#include <stdio.h>

#include "store.h"

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/tmp/gateway.db";
    sqlite3 *db = NULL;

    if (store_open(path, &db) != 0) {
        fprintf(stderr, "PROBE FAIL: store_open(%s)\n", path);
        return 1;
    }
    printf("db=%s opened\n", path);

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

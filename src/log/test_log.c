/* src/log/test_log.c —— log 模块的手写测试脚手架
 * 风格和 proto 的 test_*.c 一致：自己写 main，不用 CUnit（阶段二再迁）
 */
#include "log.h"
#include<stdio.h>
int main(void)
{
    /* 门槛设成 LOG_INFO：DEBUG 从此闭嘴 */
    if (log_init("gateway.log", LOG_INFO) != 0) {
        printf("log_init failed\n");
        return 1;
    }

    log_write(LOG_DEBUG, "this line should DISAPPEAR");
    log_write(LOG_INFO,  "hello, gateway");
    log_write(LOG_WARN,  "crc mismatch, frame dropped");
    log_write(LOG_ERROR, "something bad");
    log_close();
    return 0;
}
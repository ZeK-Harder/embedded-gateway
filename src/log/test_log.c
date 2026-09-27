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
    
    /* 每条都加上 __FILE__ 和 __LINE__ 两个参数，插在等级后面 */
    log_write(LOG_DEBUG, __FILE__, __LINE__, "this line should DISAPPEAR");
    log_write(LOG_INFO,  __FILE__, __LINE__, "hello, gateway");
    log_write(LOG_WARN,  __FILE__, __LINE__, "crc mismatch, frame dropped");
    log_write(LOG_ERROR, __FILE__, __LINE__, "something bad");
    log_close();
    return 0;
}
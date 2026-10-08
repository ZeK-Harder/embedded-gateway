/* src/log/test_log.c —— log 模块的手写测试脚手架
 * 风格和 proto 的 test_*.c 一致：自己写 main，不用 CUnit（阶段二再迁）
 */
#include "log.h"
#include <stdio.h>
int main(void)
{
    /* 注意第二个参数改名了：LOG_INFO → LOG_LV_INFO */
    if (log_init("gateway.log", LOG_LV_INFO) != 0) {
        printf("log_init failed\n");
        return 1;
    }

    LOG_DEBUG("this line should DISAPPEAR");      /* 保留：它同时是"无变参"的测试 */
    LOG_INFO ("hello, gateway");
    LOG_WARN ("crc mismatch, frame dropped");
    LOG_ERROR("something bad");
    LOG_INFO("dev %d connected, fd = %d", 7, 12);
    log_close();
    return 0;
}
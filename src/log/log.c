/* src/log/log.c —— 分级日志模块的实现 */
#include "log.h"
#include <stdio.h>      /* fopen / fclose / fputs / perror 都在这 */

/* 模块私有状态：static 表示"只在本文件可见"，别的 .c 文件看不到它 */
static FILE *g_fp = NULL;     /* 当前日志文件的那张"卡" */

/* 新加的模块状态：门槛。默认最松（全部记录），init 时再被覆盖 */
static log_level_t g_min = LOG_DEBUG;
/* 等级名映射表：下标必须与 log.h 里枚举的顺序完全一致 */
static const char *LEVEL_NAMES[] = { "DEBUG", "INFO", "WARN", "ERROR" };

int log_init(const char *path, log_level_t min_level)
{
    g_fp = fopen(path, "a");
    if (g_fp == NULL) {
        perror("log_init: fopen");
        return -1;
    }
    /*把传进来的门槛记下来，存进 g_min */
    g_min = min_level;
    return 0;
}

static const char *level_name(log_level_t lv)
{
    return LEVEL_NAMES[lv];      /* 枚举值当数组下标，直接查表 */
}

void log_write(log_level_t lv, const char *msg)
{
    /* TODO(你): 第一道防线，就 3 行
     * 门槛比较：lv 低于 g_min 就 return
     * 放在最前面，因为这是最便宜的检查，且能挡掉绝大多数调用
     */
    if (lv < g_min) {
        return;
    }
    /* 下面三行保持原样不动 */
    if (g_fp == NULL) {
        return;
    }
    if (msg == NULL) {
        return;
    }
    /* TODO(你): 一行 fprintf，格式 "[%s] %s\n"
    * 第一个 %s 喂 level_name(lv)，第二个 %s 喂 msg
    * 换行放在格式串末尾，所以不再需要单独的 fputs("\n", ...)
    */
    fprintf(g_fp, "[%s] %s\n", level_name(lv), msg);
}

void log_close(void)
{
    /* TODO(你): 三步
     * 1. 如果 g_fp 不是 NULL 才去关它（为什么？——防止重复 log_close；
     *    对 NULL 调 fclose 是未定义行为，属于"NULL 必须先判断"的同一纪律）
     * 2. 关闭后把 g_fp 置回 NULL，表示"这张卡已经还了"
     */
    if(g_fp != NULL){
        fclose(g_fp);
    }
    g_fp = NULL;
}
/* src/log/log.c —— 分级日志模块的实现 */
#include "log.h"
#include <stdio.h>      /* fopen / fclose / fputs / perror 都在这 */
#include <time.h>       /* time / localtime_r / strftime */

/* 模块私有状态：static 表示"只在本文件可见"，别的 .c 文件看不到它 */
static FILE *g_fp = NULL;     /* 当前日志文件的那张"卡" */

/* 新加的模块状态：门槛。默认最松（全部记录），init 时再被覆盖 */
static log_level_t g_min = LOG_LV_DEBUG;
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

void log_write(log_level_t lv, const char *file, int line, const char *msg)
{
    if (lv < g_min) {
        return;
    }
    if (g_fp == NULL) {
        return;
    }
    if (msg == NULL) {
        return;
    }
    time_t now = time(NULL);
    struct tm tmv;
    if (localtime_r(&now, &tmv) == NULL) {
        return;
    }
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(g_fp,"%s [%s] %s:%d %s\n", ts, level_name(lv), file, line, msg);
}

void log_close(void)
{
    /* 1. 如果 g_fp 不是 NULL 才去关它（为什么？——防止重复 log_close；
     * 对 NULL 调 fclose 是未定义行为，属于"NULL 必须先判断"的同一纪律）
     * 2. 关闭后把 g_fp 置回 NULL，表示"这张卡已经还了"
     */
    if(g_fp != NULL){
        fclose(g_fp);
    }
    g_fp = NULL;
}
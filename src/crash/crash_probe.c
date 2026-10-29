/* src/crash/crash_probe.c —— 崩溃记录器的验收探针
 *
 * 用法：./bin/crash_probe [日志路径] [segv|abort|ok]
 *   segv（默认）—— 走三层调用 probe_level_a → b → c，在 c 里解引用 NULL（触发 SIGSEGV）
 *   abort       —— 调 abort()，触发 SIGABRT
 *   ok          —— 什么都不做正常退出（验"没崩的时候日志里只有 armed 那一行"）
 *
 * 为什么要三层调用：只崩一层的话，backtrace 只有一两帧，"函数名能不能还原"验不出来。
 * 三层才看得出 backtrace 是真的在走栈。
 *
 * 为什么这几个函数不许写 static（踩过一次）：`-rdynamic` 只把**全局符号**导进动态符号表，
 * static 函数不在里面 → 日志里只剩下 `(+0x1234) [0x...]` 这种光秃秃的地址，名字全丢。
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crash.h"

void probe_level_c(void)      /* 最里层：真正踩空指针的地方 */
{
    *(volatile int *)0 = 42;
}

void probe_level_b(void)
{
    probe_level_c();
}

void probe_level_a(void)
{
    probe_level_b();
}

int main(int argc, char **argv)
{
    const char *log  = (argc > 1) ? argv[1] : NULL;   /* NULL → 默认路径 */
    const char *mode = (argc > 2) ? argv[2] : "segv";

    if (crash_install(log) != 0) {
        perror("crash_install");
        return 1;
    }
    printf("probe ready: mode=%s\n", mode);
    /* 崩溃前必须刷：stdout 重定向到文件时是全缓冲，不刷这一行就永远看不到 */
    fflush(stdout);

    if (strcmp(mode, "ok") == 0) return 0;
    if (strcmp(mode, "abort") == 0) abort();

    probe_level_a();              /* main → a → b → c → 崩 */
    printf("NOT REACHED\n");      /* 能打出来就说明 handler 之后进程还活着 = 错 */
    return 0;
}

/* sandbox/thread_log_stress.c —— 观察实验：多线程并发写日志，行会不会被撕碎
 *
 * 用途：log 模块"线程安全"这一条的验收工具。
 *       单线程写日志永远是对的，所以这个问题在 test_log.c 里看不出来——
 *       必须真的并发，才能看到 fprintf/vfprintf/fputs 三次调用之间被别的线程插进来。
 *
 * 构建（在仓库根目录执行）：
 *   gcc -Wall -Wextra -std=gnu11 -pthread -Isrc/log \
 *       src/log/log.c sandbox/thread_log_stress.c -o bin/thread_log_stress
 *
 * 运行与判定：
 *   cd bin && ./thread_log_stress && cd ..
 *   echo "总行数=$(wc -l < bin/thread.log)  期望=$((4*3000))"
 *   echo "破坏行=$(grep -cvE '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} \[INFO\] .*:15 worker [0-9]+ iteration [0-9]+ payload=-?[0-9]+$' bin/thread.log)"
 *
 * 注意正则里的 ":15" 是调用点行号——本文件里 LOG_INFO 所在的行。你改了这个文件就要同步改正则，
 * 或者干脆把行号那一段换成 .*:，只锚定后半句。
 */
#include "log.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define NTHREADS 4
#define NLOOPS   3000

static void *worker(void *arg)
{
    long id = (long)(intptr_t)arg;

    for (int i = 0; i < NLOOPS; i++) {
        /* 一行里同时有前缀、变参正文和换行，正好覆盖 log_write 的三段式写入 */
        LOG_INFO("worker %ld iteration %d payload=%d", id, i, i * 7);
    }
    return NULL;
}

int main(void)
{
    if (log_init("thread.log", LOG_LV_INFO) != 0) {
        printf("log_init failed\n");
        return 1;
    }

    pthread_t t[NTHREADS];
    for (intptr_t i = 0; i < NTHREADS; i++) {
        if (pthread_create(&t[i], NULL, worker, (void *)i) != 0) {
            printf("pthread_create failed at %d\n", (int)i);
            log_close();
            return 1;
        }
    }
    for (int i = 0; i < NTHREADS; i++) {
        pthread_join(t[i], NULL);
    }

    log_close();

    /* 日志行数应该正好是 NTHREADS * NLOOPS。少行也算失败（说明有行被覆盖或吞掉）。 */
    printf("done: 期望 %d 行，去 bin/thread.log 数一下\n", NTHREADS * NLOOPS);
    return 0;
}

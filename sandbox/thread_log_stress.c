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
 *   echo "破坏行=$(grep -cvE '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} \[INFO\] .*worker [0-9]+ iteration [0-9]+ payload=-?[0-9]+$' bin/thread.log)"
 *
 * 注意：正则只锚不变的字段，不要锚调用点行号。行号会随源码增删、
 * 甚至随二进制重编而漂移，一旦改动就得同步维护、极易失效；应只锚
 * 模块名、函数名或日志正文这类稳定片段，让匹配在改文件后依然成立。
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

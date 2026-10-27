/* test_pool.c —— 线程池骨架的验证程序（第一步：只证明 4 个 worker 能并发取任务并执行）
 *
 * 这一步不做的事（留给后面几步）：
 *   - 不抽 pool_start / pool_submit 那套对外 API（等 net 要投递任务时再定）
 *   - 不做优雅关闭（stop 标志 + broadcast + join 是单独的下一步）
 *
 * 所以这里 main 最后不 join：worker 现在永远睡在 taskq_pop_wait 里，
 * 没有任何东西能叫醒它们 —— 直接 return，进程退出时 4 个线程一起消失。
 */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "task_queue.h"

#define NWORKER 4
#define NTASK   20

static struct task_queue q;

/* 任务本体：只打印"谁干的第几个活"，用来证明任务真的被 4 个线程分走了 */
static void job(void *arg)
{
    printf("worker tid=%ld 干了任务 %ld\n", (long)pthread_self(), (long)arg);
}

/* worker 线程函数：签名必须是 void *(*)(void *)，
 * 参数就是 main 里 pthread_create 第 4 个位置传进来的那个 void *。 */
static void *worker(void *arg)
{
    struct task_queue *tq = arg;
    struct task t;

    for (;;) {
        if (taskq_pop_wait(tq, &t) != 0) break;
        t.fn(t.arg);
    }

    return NULL;   /* 理论上到不了这里 */
}

int main(void)
{
    pthread_t tid[NWORKER];

    if (taskq_init(&q, 64) != 0) { printf("init FAIL\n"); return 2; }

    /* 起 4 个 worker。第 4 个参数 &q 就是 worker 的 arg —— 队列地址。 */
    for (int i = 0; i < NWORKER; i++) {
        int rc = pthread_create(&tid[i], NULL, worker, &q);
        if (rc != 0) { printf("pthread_create 失败 rc=%d\n", rc); return 3; }
    }

    /* 投 20 个任务：fn = job，arg = 序号 */
    for (long i = 0; i < NTASK; i++) {
        struct task t;
        t.fn  = job;
        t.arg = (void *)i;
        if (taskq_push(&q, t) != 0) { printf("push 第 %ld 个失败\n", i); return 4; }
    }

    sleep(1);   /* 只是给 worker 时间吃干净队列，不是同步手段 */

    printf("size=%zu（应为 0）\n", taskq_size(&q));
    return 0;
}

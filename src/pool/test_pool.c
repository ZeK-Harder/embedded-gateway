/* test_pool.c —— 线程池骨架 + 优雅关闭的验收程序（两段，两个队列）
 *
 * 段 A（收摊时还有活没干完）：先压 20 个慢任务（每个 20ms）、再起 4 个 worker，30ms 就收摊
 *   → 期望 20 个全部执行完（收摊 ≠ 丢任务）、队列清空、4 个 worker 都 join 回来
 *   这一段盯的是 taskq_pop_wait 的语义："已收摊"不能当"立刻滚"，只有"已收摊 **且** 队列空"才返回 -1
 *
 * 段 B（收摊时大家都在睡）：全新队列、4 个 worker 先睡够 100ms，然后收摊
 *   → 期望 4 个全部被叫醒退出。这一段盯的是 taskq_stop 里的 broadcast：
 *     用 signal 只会叫醒一个，另外 3 个永远睡着，join 就等不回来
 *
 * 收尾顺序：先 stop（叫醒），再 join（等剩下的活干完再走）。
 */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "task_queue.h"

#define NWORKER 4
#define NTASK   20
#define JOB_MS  20

struct wctx {
    struct task_queue *q;
    long done;              /* 这个 worker 干了几个（收摊后由主线程求和） */
};

/* 任务本体：睡 20ms 装成慢活，再报"谁干了第几个" */
static void job(void *arg)
{
    struct timespec ts = { JOB_MS / 1000, (JOB_MS % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    printf("worker tid=%ld 干了任务 %ld\n", (long)pthread_self(), (long)arg);
}

/* worker：收到 -1 才走人（-1 的含义 = 已收摊 **且** 队列已空） */
static void *worker(void *arg)
{
    struct wctx *c = arg;
    struct task t;

    while (taskq_pop_wait(c->q, &t) == 0) {
        t.fn(t.arg);
        c->done++;
    }
    return NULL;
}

static void nap_ms(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000L * 1000L };
    nanosleep(&ts, NULL);
}

/* 段 A：收摊时队列里还剩着十几个任务 —— 一个都不许丢 */
static int phase_a(void)
{
    struct task_queue q;
    struct wctx c[NWORKER];
    pthread_t tid[NWORKER];
    long done = 0;

    if (taskq_init(&q, 64) != 0) { printf("init A FAIL\n"); return 2; }

    for (long i = 0; i < NTASK; i++) {
        struct task t;
        t.fn  = job;
        t.arg = (void *)(intptr_t)i;
        if (taskq_push(&q, t) != 0) { printf("push 第 %ld 个失败\n", i); return 4; }
    }

    for (int i = 0; i < NWORKER; i++) {
        c[i].q = &q;
        c[i].done = 0;
        if (pthread_create(&tid[i], NULL, worker, &c[i]) != 0) { printf("create A 失败\n"); return 3; }
    }

    nap_ms(30);          /* 收摊时刻：4 个都在干活，队列里还有剩 */
    taskq_stop(&q);      /* ← 本步的核心 */

    for (int i = 0; i < NWORKER; i++) pthread_join(tid[i], NULL);   /* 卡住就是收不了摊 */

    for (int i = 0; i < NWORKER; i++) done += c[i].done;
    printf("A: executed=%ld size=%zu joined=%d\n", done, taskq_size(&q), NWORKER);
    taskq_free(&q);
    return 0;
}

/* 段 B：收摊时 4 个 worker 全在 cond_wait 里睡着 —— 必须一声广播全叫起来 */
static int phase_b(void)
{
    struct task_queue q;
    struct wctx c[NWORKER];
    pthread_t tid[NWORKER];

    if (taskq_init(&q, 8) != 0) { printf("init B FAIL\n"); return 2; }

    for (int i = 0; i < NWORKER; i++) {
        c[i].q = &q;
        c[i].done = 0;
        if (pthread_create(&tid[i], NULL, worker, &c[i]) != 0) { printf("create B 失败\n"); return 3; }
    }

    nap_ms(100);         /* 让 4 个都睡进 not_empty 里 */
    taskq_stop(&q);

    for (int i = 0; i < NWORKER; i++) pthread_join(tid[i], NULL);   /* 只醒一个 → 卡在这里 */

    printf("B: joined=%d\n", NWORKER);
    taskq_free(&q);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* 被 timeout 掐掉时也别丢已经打出来的行 */

    if (phase_a() != 0) return 2;
    if (phase_b() != 0) return 2;
    return 0;
}

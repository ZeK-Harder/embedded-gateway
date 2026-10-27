#include "pool.h"

/* worker 的主循环：和 test_pool.c 里那个骨架同一个形状，
 * 区别只是队列不再来自全局，而是各自那一条。 */
static void *worker_main(void *arg)
{
    struct task_queue *q = arg;
    struct task t;

    for (;;) {
        if (taskq_pop_wait(q, &t) != 0) break;
        t.fn(t.arg);
    }
    return NULL;
}

int pool_start(struct pool *p, size_t nworkers, size_t cap)
{
    if (p == NULL || nworkers == 0 || nworkers > POOL_MAX_WORKERS) return -1;

    p->n = 0;

    /* 先把所有队列建好，再起线程：这样"起线程失败"不会留下半套结构。
     * 反过来（建一条起一个）失败时要回滚已经起来的线程，V1 不给自己找这个麻烦。 */
    for (size_t i = 0; i < nworkers; i++) {
        if (taskq_init(&p->w[i].q, cap) != 0) {
            for (size_t j = 0; j < i; j++) taskq_free(&p->w[j].q);
            return -1;
        }
    }

    for (size_t i = 0; i < nworkers; i++) {
        if (pthread_create(&p->w[i].tid, NULL, worker_main, &p->w[i].q) != 0) {
            /* 线程起不来就停在这里：已经起来的那些不 join，由进程退出收尾（V1 边界） */
            return -1;
        }
        p->n++;
    }
    return 0;
}

int pool_submit(struct pool *p, size_t idx, struct task t)
{
    if (p == NULL || p->n == 0) return -1;
    return taskq_push(&p->w[idx % p->n].q, t);
}

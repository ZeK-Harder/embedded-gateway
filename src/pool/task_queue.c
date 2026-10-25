#include "task_queue.h"

#include <stdlib.h>

int taskq_init(struct task_queue *q, size_t cap)
{
    if (q == NULL || cap == 0) return -1;
    q->items = malloc(cap * sizeof *q->items);
    if (q->items == NULL) {
        q->cap = 0;
        q->head = 0;
        q->tail = 0;
        q->count = 0;
        return -1;
    }
    pthread_mutex_init(&q->lock, NULL);
    q->cap   = cap;
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    return 0;
}

void taskq_free(struct task_queue *q)
{
    pthread_mutex_destroy(&q->lock);
    free(q->items);
    q->items = NULL;
    q->cap = q->head = q->tail = q->count = 0;
}

size_t taskq_size(struct task_queue *q)
{
    size_t n;

    pthread_mutex_lock(&q->lock);
    n = q->count;                            /* 只在锁里读一次，读到的就是那一刻的准确值 */
    pthread_mutex_unlock(&q->lock);

    return n;
}

int taskq_push(struct task_queue *q, struct task t)
{
    int rc = 0;

    pthread_mutex_lock(&q->lock);                     /* 一进函数就上锁 */
    if (q->count == q->cap) { rc = -1; goto out; }    /* 满：记下 -1 去 out，不是 return */
    q->items[q->tail] = t;
    q->tail = (q->tail + 1) % q->cap;
    q->count++;
out:
    pthread_mutex_unlock(&q->lock);                   /* 任何一条路都从这放锁 */
    return rc;
}

int taskq_pop(struct task_queue *q, struct task *out)
{
    int rc = 0;

    pthread_mutex_lock(&q->lock);                     /* 一进函数就上锁 */
    if (q->count == 0) { rc = -1; goto out; }    /* 空：记下 -1 去 out，不是 return */
    *out = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
out:
    pthread_mutex_unlock(&q->lock);                   /* 任何一条路都从这放锁 */
    return rc;
}

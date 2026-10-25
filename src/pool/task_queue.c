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
    q->cap   = cap;
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    return 0;
}

void taskq_free(struct task_queue *q)
{
    free(q->items);
    q->items = NULL;
    q->cap = q->head = q->tail = q->count = 0;
}

size_t taskq_size(const struct task_queue *q)
{
    return q->count;
}

int taskq_push(struct task_queue *q, struct task t)
{
    if (q->count == q->cap) return -1;
    q->items[q->tail] = t;                 /* 写进 tail 指的那一格 */
    q->tail = (q->tail + 1) % q->cap;      /* 回绕：写满最后一格后回到 0 */
    q->count++;
    return 0;
}

int taskq_pop(struct task_queue *q, struct task *out)
{
    if (q->count == 0) return -1;
    *out = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return 0;
}

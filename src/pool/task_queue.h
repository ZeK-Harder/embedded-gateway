#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <stddef.h>

typedef void (*task_fn)(void *arg);

struct task {
    task_fn fn;    /* 要执行哪个函数 */
    void   *arg;   /* 交给它什么参数 */
};

struct task_queue {
    struct task *items;   /* 槽位数组，init 时按 cap 分配 */
    size_t cap;           /* 一共几格 */
    size_t head;          /* 下一次出队从哪一格取 */
    size_t tail;          /* 下一次入队往哪一格写 */
    size_t count;         /* 现在里面有几个任务 */
};

int    taskq_init(struct task_queue *q, size_t cap);   /* 成功 0，失败 -1 */
void   taskq_free(struct task_queue *q);
size_t taskq_size(const struct task_queue *q);         /* 现在几个任务 */
int    taskq_push(struct task_queue *q, struct task t);/* 成功 0，满 -1 */
int    taskq_pop(struct task_queue *q, struct task *out); /* 成功 0，空 -1 */

#endif

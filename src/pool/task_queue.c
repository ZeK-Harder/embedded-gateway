#include "task_queue.h"

#include <stdlib.h>

/* 建队列：容量由调用方给（不要写死宏 —— 队列多深、worker 几个，是上层策略）。
 *
 * 失败路径的规矩（2026-10-25 实测）：malloc 失败时也要把字段清零，
 * 让队列处于"可以安全应答的空态"。这样即使调用方忘了看返回值，
 * 后面的 push/pop 也会靠 count/cap 判断安全返回 -1，
 * 而不是拿着栈上的垃圾指针去解引用（实测后果：段错误，退出码 139）。
 *
 * 顺序：malloc 成功之后才建锁和条件变量 —— 失败那条路压根不该留下要销毁的锁。
 */
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
    pthread_cond_init(&q->not_empty, NULL);
    q->cap   = cap;
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    return 0;
}

/* 销毁顺序与 init 相反：先条件变量、再锁、最后释放槽位。
 * 前提：调用它之前，必须保证没有别的线程还在 push/pop ——
 * 否则它们会去操作一把已经被销毁的锁（未定义行为，通常是崩或挂死）。 */
void taskq_free(struct task_queue *q)
{
    pthread_cond_destroy(&q->not_empty);
    pthread_mutex_destroy(&q->lock);
    free(q->items);
    q->items = NULL;
    q->cap = q->head = q->tail = q->count = 0;
}

/* 两个"为什么"：
 * ① 为什么参数不是 const —— pthread_mutex_lock 的入参是 pthread_mutex_t *，
 *    而 const 结构体里的成员也是 const，传不进去（gcc: -Wdiscarded-qualifiers）。
 *    语义上它也确实不再是只读：取锁就是在改锁自己的状态。
 * ② 为什么要进锁 —— count 正被别的线程改；不在锁里读，可能读到改了一半的值。
 */
size_t taskq_size(struct task_queue *q)
{
    size_t n;

    pthread_mutex_lock(&q->lock);
    n = q->count;                            /* 只在锁里读一次，读到的就是那一刻的准确值 */
    pthread_mutex_unlock(&q->lock);

    return n;
}

/* 入队：满了返回 -1，不等待。
 * 为什么不让它等：调用方是 epoll 的 IO 线程，"等队列腾出空位"会把整个事件循环卡住 ——
 * 宁可让上层丢一个任务并记一行日志（和 net 里 wbuf 满就整帧丢弃同一个思路）。
 *
 * signal 放锁内还是放锁后：两种都正确。
 *   锁内 → 被叫醒的消费者会先卡在 lock 上（多一次"唤醒-阻塞"），但出口只有一条，不会漏；
 *   锁后 → 少这一次唤醒，反正数据已经写完了。
 * 这里选锁内，图的是"只有一个出口，不可能忘"。
 */
int taskq_push(struct task_queue *q, struct task t)
{
    int rc = 0;

    pthread_mutex_lock(&q->lock);                     /* 一进函数就上锁 */
    if (q->count == q->cap) { rc = -1; goto out; }    /* 满：记下 -1 去 out，不是 return */
    q->items[q->tail] = t;
    q->tail = (q->tail + 1) % q->cap;                 /* 回绕：写满最后一格后回到 0 */
    q->count++;
    pthread_cond_signal(&q->not_empty);               /* 叫醒"至少一个"在 wait 的消费者 */
out:
    pthread_mutex_unlock(&q->lock);                   /* 任何一条路都从这里放锁 */
    return rc;
}

/* 非阻塞出队：空就返回 -1，不等。
 * 单线程探针、以及"IO 线程绝不能等"的场合用它。
 * 单出口 goto out 的理由：函数里有几个 return，就有几处可能漏 unlock。 */
int taskq_pop(struct task_queue *q, struct task *out)
{
    int rc = 0;

    pthread_mutex_lock(&q->lock);                     /* 一进函数就上锁 */
    if (q->count == 0) { rc = -1; goto out; }         /* 空：记下 -1 去 out，不是 return */
    *out = q->items[q->head];                         /* 判空 + 取数据必须在同一把锁里：
                                                       * 只锁 count-- 的话，两个消费者会
                                                       * 各自判空通过、再各取走同一格（重复任务） */
    q->head = (q->head + 1) % q->cap;
    q->count--;
out:
    pthread_mutex_unlock(&q->lock);                   /* 任何一条路都从这里放锁 */
    return rc;
}

/* 阻塞出队：空就睡在 not_empty 上，直到 push 里 signal 把你叫醒。
 *
 * 为什么判空用 while 不用 if —— 两条硬理由：
 *   ① POSIX 允许"虚假唤醒"：wait 可能无故返回，醒来时条件未必成立；
 *   ② signal 只保证叫醒"至少一个"：4 个消费者被叫醒 2 个时，
 *      第一个抢到锁取走任务、count 变 0，第二个醒来条件已不成立 ——
 *      if 会直接往下走去取一个不存在的任务（读出上一轮的残值，就是"pop 出 50 循环"那种现象），
 *      while 会再判一次继续睡。
 *
 * pthread_cond_wait 内部做了两件事：原子地「放开 q->lock + 睡下」；
 * 被叫醒时「先重新抢回 q->lock」才返回 —— 所以它返回时你仍然是持锁的，后面照常 unlock。
 * 如果它不放锁，push 永远进不来，就是死锁。
 */
int taskq_pop_wait(struct task_queue *q, struct task *out)
{
    int rc = 0;

    pthread_mutex_lock(&q->lock);
    while (q->count == 0)
        pthread_cond_wait(&q->not_empty, &q->lock);
    *out = q->items[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    pthread_mutex_unlock(&q->lock);
    return rc;
}

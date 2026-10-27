#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <stddef.h>
#include <pthread.h>

/* ==========================================================================
 * task_queue.h —— 线程安全的任务队列（pool 模块的第一块砖）
 *
 * 【为什么要有它】
 *   net 的 IO 线程收到一整帧后不能就地做耗时活（比如写 SQLite 要落盘，几毫秒到几十毫秒）。
 *   它只能把「要干什么」打包成一个 struct task 塞进队列，由 worker 线程取走执行 ——
 *   这样 epoll_wait 不被业务拖住，所有连接才不会一起变慢。
 *
 * 【三个零件各管一件事，不许混】
 *   pthread_mutex_t lock      —— 保护 items/head/tail/count（谁碰这份数据谁上锁）
 *   pthread_cond_t  not_empty —— 只传"新任务来了"这个消息，叫醒睡着的消费者；
 *                                它不保护任何数据，必须配 lock 才能用
 *   struct task { fn, arg }   —— 一个可排队的动作：调哪个函数 + 给它什么参数
 *
 * 【两个出队函数语义不同，别用错】
 *   taskq_pop       空 → 立刻返回 -1（非阻塞）。单线程探针、"IO 线程绝不能等"的场合用它。
 *   taskq_pop_wait  空 → 睡在 not_empty 上，被 push 叫醒（worker 线程用它，不烧 CPU）。
 *
 * 【环形队列的不变量（三个字段必须始终自洽）】
 *   0 <= count <= cap      且      tail == (head + count) % cap
 *   —— 所以"空/满"只能问 count，永远别拿 head/tail 去猜。
 * ========================================================================== */

typedef void (*task_fn)(void *arg);

struct task {
    task_fn fn;    /* 要执行哪个函数 */
    void   *arg;   /* 交给它什么参数（谁申请谁释放，由业务约定） */
};

struct task_queue {
    pthread_mutex_t lock;       /* 守下面所有字段：只要可能被两个线程同时碰，读写都要进锁 */
    pthread_cond_t  not_empty;  /* 只在 push 之后 signal：告诉等着的消费者"有活干了" */
    struct task *items;         /* 槽位数组，init 时按 cap 分配 */
    size_t cap;                 /* 一共几格 */
    size_t head;                /* 下一次出队从哪一格取 */
    size_t tail;                /* 下一次入队往哪一格写 */
    size_t count;               /* 现在里面有几个任务（"空/满"只能问它） */
    int    stopping;            /* 0 = 正常营业；1 = 已下令收摊（只在锁内改、锁内读） */
};

/* 成功 0；cap 为 0 或内存不够 -1。失败时队列处于"可安全应答的空态"（不崩） */
int    taskq_init(struct task_queue *q, size_t cap);

/* 销毁条件变量、锁、释放槽位；之后不能再用 q（且必须确认没有线程还在用） */
void   taskq_free(struct task_queue *q);

/* 现在几个任务（锁内读，返回的是那一刻的快照） */
size_t taskq_size(struct task_queue *q);

/* 入队：成功 0，满 -1（不等待 —— 调用方是 IO 线程，不许被阻塞） */
int    taskq_push(struct task_queue *q, struct task t);

/* 非阻塞出队：成功 0，空 -1 */
int    taskq_pop(struct task_queue *q, struct task *out);

/* 阻塞出队：空则睡，被 push 叫醒；成功 0
 * 收摊之后（taskq_stop 过）→ 队列空的就返回 -1，让 worker 走人；
 * 队列里还剩活的话仍然要取完（收摊 ≠ 丢任务）。 */
int    taskq_pop_wait(struct task_queue *q, struct task *out);

/* 下令收摊：锁内置 stopping = 1，再 broadcast 叫醒**所有**睡在 not_empty 上的人。
 * 幂等；调用之后不许再 push。 */
void   taskq_stop(struct task_queue *q);

#endif

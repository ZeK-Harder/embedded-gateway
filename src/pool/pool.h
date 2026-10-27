#ifndef POOL_H
#define POOL_H

#include <stddef.h>
#include <pthread.h>

#include "task_queue.h"

/* ==========================================================================
 * pool.h —— 线程池（N 个 worker，每个 worker 一条自己的任务队列）
 *
 * 【为什么不是"一条队列 + N 个 worker 抢"】
 *   抢式队列吞吐高，但同一个连接的两帧可能被不同 worker 取走，
 *   处理完成的先后顺序就乱了（io 线程按完成顺序发回，网络上看到的就是乱序）。
 *   网关要的是"同一连接内保序、不同连接并行"，所以按连接分片：
 *   IO 线程用 `连接槽位号 % worker 数` 选队列 —— 同一个连接永远落在同一条队列、
 *   由同一个 worker 按 FIFO 处理，顺序天然保住；不同连接分散到不同 worker，并发还在。
 *
 * 【失败策略与 task_queue 一致】
 *   pool_submit 不等待：队列满就返回 -1，由调用方（IO 线程）丢帧并记日志。
 *   IO 线程绝不能被阻塞，这条和 wbuf 满就整帧丢弃是同一个思路。
 *
 * 【V1 边界】
 *   pool_stop（stop 标志 + broadcast + join）还没做 —— 见优雅关闭那一步。
 *   现在进程退出时 worker 被 exit_group 一起带走。
 * ========================================================================== */

#define POOL_MAX_WORKERS 8

struct pool_worker {
    struct task_queue q;      /* 这个 worker 专用的队列 */
    pthread_t         tid;
};

struct pool {
    size_t n;                              /* 实际起了几个 worker */
    struct pool_worker w[POOL_MAX_WORKERS];
};

/* 成功 0；参数非法 / 某条队列 init 失败 / 某个线程起不来 -1（失败时不留半成品） */
int  pool_start(struct pool *p, size_t nworkers, size_t cap);

/* 投到第 idx % n 个 worker 的队列：成功 0，满 -1 */
int  pool_submit(struct pool *p, size_t idx, struct task t);

#endif

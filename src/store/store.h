#ifndef STORE_H
#define STORE_H

#include <sqlite3.h>
#include <pthread.h>    /* store_batch_t 里的互斥锁 */

/* 打开（或新建）一个库文件。成功 rc=0 且 *out 可用；失败 -1 且 *out 不变。 */
int store_open(const char *path, sqlite3 **out);

/* 关库。传 NULL 直接返回，不崩。 */
void store_close(sqlite3 *db);

/* 建表（幂等：已存在就当成功）。成功 0，失败 -1。 */
int store_init_schema(sqlite3 *db);

/* 插一条事件。返回值（调用方靠它决定"重试 / 丢弃 / 报警"，所以必须区分）：
 *   0  = 写进库了
 *  -1  = 数据本身不合规（约束冲突等）→ 调用方丢掉这条帧，继续跑
 *  -2  = 库暂时被锁（SQLITE_BUSY）→ 调用方稍后重试；本函数不自己重试
 *  -3  = 库写不进去（磁盘满 / IO 错 / 库损坏）→ 调用方报警并停止落库
 */
int store_insert_event(sqlite3 *db, int dev_id, long long ts, int func, int val);

/* 每查出一行，store 层就回调一次，把该行交给你（打印 / 塞缓冲 / 丢掉都行）。 */
typedef void (*store_row_cb)(void *ctx, int dev_id, long long ts, int func, int val);

/* 查某台设备最近 limit 条事件，按 ts 从新到旧。
 * 返回值与上面一致：0 成功 / -1 SQL 错 / -2 BUSY / -3 其他。
 * cb 传 NULL 时只数行不回调（后面要"查最近 N 条"的计数也用得上）。 */
int store_query_recent(sqlite3 *db, int dev_id, int limit, store_row_cb cb, void *ctx);

/* ===== 批量事务（2026-10-31 学生选 B）=====
 * 背景（压测实测）：逐帧一个事务 ⇒ 每帧一次 fsync。50 连接实测服务端只处理 ~880 帧/秒，
 *   回包积压后被整帧丢弃，错误率 38.6%；同一份负载把库换成 tmpfs（fsync 几乎免费）错误率 0.000%。
 * 修法：把 STORE_BATCH_MAX 条 INSERT 并进一个事务，提交次数除以 64。
 * 丢数据窗口：进程崩 / 断电最多丢「还没 COMMIT 的那一批」（≤64 帧或 ≤50ms）；已提交的一条不丢。
 * 线程安全：内部一把互斥锁，多个 worker 可以同时调 store_insert_event_batched。
 */
#define STORE_BATCH_MAX  64
#define STORE_BATCH_MS   50

typedef struct {
    sqlite3        *db;
    int             in_tx;            /* 1 = 事务已 BEGIN，等 COMMIT */
    int             pending;          /* 事务里攒了几条 */
    long long       last_commit_ms;   /* 上次提交的单调时钟毫秒 */
    pthread_mutex_t mu;
} store_batch_t;

int  store_batch_init(store_batch_t *b, sqlite3 *db);
int  store_insert_event_batched(store_batch_t *b, int dev_id, long long ts, int func, int val);
int  store_batch_flush(store_batch_t *b);
void store_batch_close(store_batch_t *b);

#endif

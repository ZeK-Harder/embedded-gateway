#define _POSIX_C_SOURCE 200809L
// ---------- 头文件（一行一个来源）----------
#include <stdio.h>          // printf
#include <string.h>         // memset
#include <unistd.h>        // read / close / sleep
#include <sys/socket.h>    // socket / bind / listen / accept
#include <netinet/in.h>    // struct sockaddr_in
#include <arpa/inet.h>     // htons / htonl / inet_addr
#include <errno.h>         // errno, EAGAIN, EWOULDBLOCK, EINTR
#include <stdlib.h>        // exit / EXIT_FAILURE
#include <time.h>          // time：落库的时间戳
#include <sys/epoll.h>     // epoll_create1 / epoll_ctl / epoll_wait
#include <fcntl.h>         // fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include "proto.h"
#include "pool.h"
#include "store.h"
#include <signal.h>
#include <sys/eventfd.h>

#define MAX_CONN 1024
#define POOL_WORKERS   4        /* worker 个数：按连接槽位分片 */
#define POOL_QUEUE_CAP 65536    /* 每条队列能压多少帧（16B/格，约 1MB）—— 要装得下一次突发 */

struct conn {
    int fd;
    int total;          /* 这条连接累计读到多少字节 */
    int used;           /* 槽位是否被占用：0 = 空槽 */
    int gen;            /* 槽位代数：每被领用一次 +1，用来判断"任务还认得这条连接吗" */
    unsigned char rbuf[4096];   /* 这条连接攒下的原始字节（半成品柜） */
    size_t rlen;         /* 柜台里现在有多少字节 */
    frame_parser_t fp;   /* 这条连接自己的切包状态机：跨 recv 的半帧住在这里 */
    unsigned char wbuf[8192];
    size_t wlen;   /* 待发字节数 */
    size_t woff;   /* 已发位置 */
    int out_on;
};

static struct conn conns[MAX_CONN];
static volatile sig_atomic_t g_stop = 0;      /* 只允许是这一种类型 */
static void on_stop(int sig) { (void)sig; g_stop = 1; }   /* handler 里只写这一句 */

/* ---------- IO 线程只投递：线程池 + 回投通道 ----------
 *   g_pool    4 个 worker，投递时按连接槽位选队列（同一连接保序，不同连接并行）
 *   g_outq    反方向队列：worker 处理完把"要发的帧"投回来，由 IO 线程统一 send
 *   g_efd     eventfd：worker 投完写 8 字节，把阻塞在 epoll_wait 的 IO 线程叫醒
 *   g_efd_tag 事件循环里用来认出"这是 eventfd"（门用的是 NULL）
 */
static struct pool       g_pool;
static struct task_queue g_outq;
static int  g_efd = -1;
static int  g_epfd = -1;
static char g_efd_tag;

static sqlite3 *g_db = NULL;   /* 落库：全进程一个连接（sqlite3 默认 serialized，多线程共享安全） */

/* 一次投递带走的东西：帧字节的副本 + "我是哪条连接"（槽位 + 代数） */
struct frame_job {
    int     slot;
    int     gen;
    int     fd;          /* 只给日志用 */
    size_t  len;
    uint8_t buf[];       /* 柔性数组：malloc 一次就把帧字节带上，不像 rbuf 那样会被下轮 recv 覆盖 */
};

/* 从池子里拿一个空槽；拿不到返回 NULL */
static struct conn *conn_alloc(void)
{
    for (int i = 0; i < MAX_CONN; i++)
        if (!conns[i].used) {
            conns[i].used  = 1;
            conns[i].gen  += 1;        /* 换一代：上一条连接留下的任务立刻失效 */
            conns[i].fd    = -1;
            conns[i].total = 0;
            conns[i].rlen  = 0;
            /* 清理放在唯一的"分配入口"，不放在"释放出口"：
             * 槽位要变成某条连接的只有这一条路，放这里物理上不可能漏；
             * 而关闭路径已经有 3 条（FIN / recv 错误 / 缓冲满），以后还会加心跳超时、写失败 --
             * 放出口就得每条都记得补一次。 */
            /* 写缓冲也要在领用时清空 —— 槽位复用时它里面可能还躺着上一条连接的字节 */
            conns[i].wlen  = 0;
            conns[i].woff  = 0;
            conns[i].out_on = 0;   /* 新加到结构体里的状态，领用时一律清 */
            frame_parser_init(&conns[i].fp);
            return &conns[i];
        }
    return NULL;
}

static void conn_free(struct conn *c)
{
    c->used = 0;
}

/* 改这条连接感兴趣的事件 */
static void conn_set_events(int epfd, struct conn *c, uint32_t evs)
{
    struct epoll_event e;
    memset(&e, 0, sizeof e);
    e.events   = evs;
    e.data.ptr = c;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &e) < 0 && errno != ENOENT)
        perror("epoll_ctl MOD");
}

/* 把 wbuf 里的字节尽量发出去
 *   返回  0 = 发完了（并且已经把 EPOLLOUT 撤掉）
 *   返回  1 = 内核发送缓冲满了，剩下的留在 wbuf，等 EPOLLOUT 回来再发
 *   返回 -1 = 真错误（调用方负责关连接）
 */
static int conn_flush(int epfd, struct conn *c)
{
    while (c->woff < c->wlen) {
        ssize_t n = send(c->fd, c->wbuf + c->woff, c->wlen - c->woff, MSG_NOSIGNAL);

        if (n > 0) {
            c->woff += (size_t)n;      /* 发出去多少就前进多少，否则会永远重发同一批字节 */
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;                  /* 被信号打断：重试，不算错 */
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* 内核发送缓冲满了：不是错误，是"现在写不进去"。
             * 注册 EPOLLOUT，等 socket 变可写再回来接着发。 */
            conn_set_events(epfd, c, EPOLLIN | EPOLLOUT | EPOLLET);
            c->out_on = 1;
            return 1;
        }
        return -1;                     /* EPIPE / ECONNRESET … 真正的失败 */
    }

    c->woff = 0;
    c->wlen = 0;
    /* 发完必须撤掉 EPOLLOUT：socket 平时一直是"可写"的，
     * 不撤的话 epoll_wait 每次都报可写 -> 空转烧 CPU（和 EMFILE 那次同类）。 */
    if (c->out_on) {
        conn_set_events(epfd, c, EPOLLIN | EPOLLET);
        c->out_on = 0;
    }
    return 0;
}

/* 把一整帧追加进写缓冲，然后立刻尝试发出去
 *   返回 0 / 1 同 conn_flush；返回 -2 = 缓冲满了（本步策略：整帧丢弃）
 */
static int conn_queue(int epfd, struct conn *c, const uint8_t *data, size_t len)
{
    /* 缓冲满 = 对端读得太慢。V1 策略：整帧丢掉，绝不写半个帧进去 ——
     * 帧边界比"少收一帧"重要得多（少一帧只是数据缺失，半个帧会让后面全乱）。
     * 真正一帧不丢需要背压（写满时暂停读这条连接），属于 V2。 */
    if (sizeof c->wbuf - c->wlen < len) {
        return -2;
    }
    memcpy(c->wbuf + c->wlen, data, len);
    c->wlen += len;
    return conn_flush(epfd, c);
}

/* ---------------- IO 线程只投递：两条边各管一半 ----------------
 *
 *   IO 线程（epoll） ：收字节 → 切帧 → malloc 一份帧副本 → pool_submit
 *   worker 线程      ：proto_decode → 业务（这里只有 printf）→ 投回 g_outq → 敲 eventfd
 *   IO 线程（eventfd）：从 g_outq 取出 → conn_queue → 真正 send
 *
 * 为什么是"回投"而不是 worker 直接 send：wbuf / woff / wlen 只有 IO 线程碰，
 * 就完全不需要锁；worker 再多也不会两个人同时改同一条连接的写缓冲。
 */

/* IO 线程侧：把 worker 交回来的帧真正发出去。
 * 先验"这条连接还是不是我认的那一条"：槽位可能已被回收、又被新连接复用，
 * 代数（gen）就是用来区分"同一个座位上的两个不同客人"的。 */
static void send_back(void *arg)
{
    struct frame_job *fj = arg;
    struct conn *c = NULL;

    if (fj->slot >= 0 && fj->slot < MAX_CONN &&
        conns[fj->slot].used && conns[fj->slot].gen == fj->gen) {
        c = &conns[fj->slot];
    }
    if (c != NULL) {
        int q = conn_queue(g_epfd, c, fj->buf, fj->len);
        if (q == -2) {
            printf("drop: 写缓冲满 fd=%d（对端读太慢，整帧丢弃）\n", c->fd);
        } else if (q < 0) {
            perror("send");
        }
    } else {
        printf("drop: 连接已关闭 slot=%d（回投的帧作废）\n", fj->slot);
    }
    free(fj);
}

/* 排空回投队列。顺序很重要：先取队列、再读 eventfd。
 * 反过来（先读 eventfd 再取队列）有个缝隙：worker 正好在我们读完之后 push + 敲，
 * 计数被这次 read 清掉，于是队列里有东西却没人再叫醒 IO 线程 —— 卡住的经典写法。 */
static void drain_outq(void)
{
    struct task t;

    for (;;) {
        while (taskq_pop(&g_outq, &t) == 0) t.fn(t.arg);

        uint64_t v;
        if (read(g_efd, &v, sizeof v) <= 0) break;   /* EAGAIN = 真排空了 */
    }
}

/* worker 侧：解码 + 业务。解码失败就把这份内存还掉，不往下传。 */
static void frame_job_run(void *arg)
{
    struct frame_job *fj = arg;

    uint8_t  dev  = 0;
    uint8_t  func = 0;
    uint16_t seq  = 0;
    uint8_t  payload[PROTO_PAYLOAD_MAX];
    size_t   plen = 0;

    if (proto_decode(fj->buf, fj->len, &dev, &func, &seq, payload, sizeof payload, &plen) != 0) {
        /* 数据坏 != 通道坏：只丢这一帧，连接和后面的帧都继续 */
        printf("drop: 解码失败 fd=%d\n", fj->fd);
        free(fj);
        return;
    }

    printf("frame dev=%u func=%u seq=%u plen=%zu tid=%ld\n",
           (unsigned)dev, (unsigned)func, (unsigned)seq, plen, (long)pthread_self());

    /* 落库：这一帧写进 events。IO 线程一行都不碰库 —— 磁盘再慢也只慢 worker。
     * val 先取 payload 的第一个字节（以后按 func 再解析具体含义）。 */
    int val  = (plen > 0) ? (int)payload[0] : 0;
    int sret = store_insert_event(g_db, (int)dev, (long long)time(NULL), (int)func, val);
    if (sret == -1) {
        /* 数据不合规：丢这一帧，连接和后面的帧都继续 */
        printf("store: 数据不合规，丢这一帧 fd=%d seq=%u\n", fj->fd, (unsigned)seq);
    } else if (sret == -2) {
        /* 库被别的连接锁着：这帧先丢，由调用方那层再决定要不要重试（V1 不重试） */
        printf("store: 库忙，这帧没写进去 fd=%d seq=%u\n", fj->fd, (unsigned)seq);
    } else if (sret != 0) {
        /* -3 磁盘满 / IO 错：V1 只报不停写 */
        printf("store: 库写不进去 rc=%d fd=%d seq=%u\n", sret, fj->fd, (unsigned)seq);
    }

    struct task t = { send_back, fj };
    if (taskq_push(&g_outq, t) != 0) {
        printf("drop: 回投队列满 fd=%d（整帧丢弃）\n", fj->fd);
        free(fj);
        return;
    }

    uint64_t one = 1;
    if (write(g_efd, &one, sizeof one) < 0 && errno != EAGAIN) {
        perror("write eventfd");   /* 唤不醒也不会丢：IO 线程下次醒来仍会排空队列 */
    }
}

int main(int argc, char **argv) {
    /* 库路径可以由命令行给，方便验收脚本用一个干净的库 */
    const char *db_path = (argc > 1) ? argv[1] : "/tmp/gateway.db";

    setvbuf(stdout, NULL, _IOLBF, 0);   //输出重定向到文件时否则全缓冲，脚本读不到

    // ---------- 落库：开库 + 建表（启动阶段失败 = 程序干不了活，直接死）----------
    if (store_open(db_path, &g_db) != 0) {
        fprintf(stderr, "store_open(%s) 失败\n", db_path);
        exit(EXIT_FAILURE);
    }
    if (store_init_schema(g_db) != 0) {
        fprintf(stderr, "store_init_schema 失败\n");
        store_close(g_db);
        exit(EXIT_FAILURE);
    }
    printf("store db=%s ready\n", db_path);


    // ---------- 造门 ----------
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    } else {
        printf("socket 返回 %d\n", listen_fd);
    }

    int on = 1;                                     // SO_REUSEADDR 的开关值：1 = 打开
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) < 0) {
        perror("setsockopt SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(8090);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int r = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (r < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    } else {
        printf("bind 返回 %d\n", r);
    }

    int r2 = listen(listen_fd, 8);
    if (r2 < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    } else {
        printf("listen 返回 %d\n", r2);
    }

    // ---------- 造簿子 + 把门登记进去 ----------
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;
    ev.events   = EPOLLIN;
    ev.data.ptr = NULL;
    // 门登记 —— 启动阶段失败 = 这程序废了，直接死（和 bind 一致）
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        perror("epoll_ctl ADD 门");
        exit(EXIT_FAILURE);
    }

    /* ---------- 起线程池 + 造回投通道 ---------- */
    g_epfd = epfd;
    if (taskq_init(&g_outq, POOL_QUEUE_CAP) != 0) {
        perror("taskq_init g_outq");
        exit(EXIT_FAILURE);
    }
    if (pool_start(&g_pool, POOL_WORKERS, POOL_QUEUE_CAP) != 0) {
        fprintf(stderr, "pool_start 失败\n");
        exit(EXIT_FAILURE);
    }
    g_efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (g_efd < 0) {
        perror("eventfd");
        exit(EXIT_FAILURE);
    }
    ev.events   = EPOLLIN;
    ev.data.ptr = &g_efd_tag;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, g_efd, &ev) < 0) {
        perror("epoll_ctl ADD eventfd");
        exit(EXIT_FAILURE);
    }
    printf("io tid=%ld  worker=%d\n", (long)pthread_self(), POOL_WORKERS);

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // ---------- 事件循环 ----------
    /* parser 凑齐一整帧时，把这帧的线上字节拷进 frame_buf，长度写进 flen。
     * 放在事件循环外面复用即可：半帧住在 c->fp 里，整帧只在"刚解出来那一瞬"需要一块地方。 */
    uint8_t frame_buf[PROTO_FRAME_WIRE_MAX];   /* 514 字节 = 线上帧上限 */
    size_t  flen = 0;
    struct epoll_event evs[8];
    while (1) {
        int n = epoll_wait(epfd, evs, 8, -1);
        if (n < 0) {
            if (errno == EINTR) {
                if (g_stop) break;      /* 是"该退出"那个信号：退主循环 */
                continue;               /* 别的信号：只算被打断，重来 */
            }
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            struct conn *c = evs[i].data.ptr;

            if ((void *)c == (void *)&g_efd_tag) {   // worker 投回了要发的帧
                drain_outq();
            } else if (c == NULL) {     // 有人来连
                int conn_fd = accept(listen_fd, NULL, NULL);
                if (conn_fd < 0) {
                    /* 抖动类：立刻重试（对端握手期跑了 / 被信号打断 / 队列空了） */
                    if (errno == EINTR || errno == ECONNABORTED ||
                        errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    /* 资源耗尽：报原因 + 退避。继续空转只会烧 CPU + 刷屏 */
                    if (errno == EMFILE || errno == ENFILE) {
                        perror("accept (fd 耗尽)");
                        sleep(1);
                        continue;          /* 必须 continue，不能漏到下面的 perror */
                    }
                    perror("accept");
                    continue;
                }

                // 设为非阻塞
                int flags = fcntl(conn_fd, F_GETFL, 0);
                if (flags < 0) {
                    perror("fcntl F_GETFL");
                    close(conn_fd);
                    continue;
                }
                if (fcntl(conn_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    perror("fcntl F_SETFL");
                    close(conn_fd);
                    continue;
                }

                printf("新连接 conn_fd=%d (非阻塞)\n", conn_fd);
                c = conn_alloc();
                if (!c) {
                    close(conn_fd);
                    continue;
                }
                c->fd = conn_fd;

                ev.events  = EPOLLIN | EPOLLET;
                ev.data.ptr = c;
                // 新连接登记 —— 登记不上就别把这条连接留着
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev) < 0) {
                    perror("epoll_ctl ADD conn");
                    conn_free(c);
                    close(conn_fd);
                    continue;
                }
                printf("已登记 conn_fd=%d\n", conn_fd);

            } else {                          // 已连接的客人
                /* 先处理"可写"：上次没发完的字节，现在能接着发了。 */
                if (evs[i].events & EPOLLOUT) {
                    if (conn_flush(epfd, c) < 0) {
                        perror("flush");
                        if (epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL) < 0 && errno != ENOENT)
                            perror("epoll_ctl DEL");
                        conn_free(c);
                        close(c->fd);
                        continue;
                    }
                }
                /* 这次事件如果只有"可写"，那就没有可读的事，别往下走读循环 */
                if (!(evs[i].events & EPOLLIN)) {
                    continue;
                }

                ssize_t m;
                int fd = c->fd;
                while (1) {
                    if (c->rlen == sizeof c->rbuf) {
                        if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0 && errno != ENOENT)
                            perror("epoll_ctl DEL");
                        conn_free(c);
                        close(fd);
                        break;
                    }
                    m = recv(fd, c->rbuf + c->rlen, sizeof c->rbuf - c->rlen, 0);
                    if (m > 0) {
                        /* 本次 recv 的 m 个字节落进 rbuf 的 [start, start+m)；
                         * rlen 记"缓冲里现在一共有多少字节"，total 只用来观察累计量。 */
                        size_t start = c->rlen;
                        c->rlen  += (size_t)m;
                        c->total += (int)m;

                        /* 逐字节喂进状态机：frame_parser_feed 的接口就是"一次一个字节"。
                         * 它内部自己攒——不够一帧返回 0；够一帧返回 1，并把这帧拷进 frame_buf。 */
                        for (size_t bi = start; bi < c->rlen; bi++) {
                            int rc = frame_parser_feed(&c->fp, c->rbuf[bi],frame_buf, sizeof frame_buf, &flen);
                            if (rc == 1) {                    /* 凑齐一整帧了 */
                                /* 不在 IO 线程里解码了：把这一帧连同"我是谁"打包投给 worker。
                                 * 帧字节必须复制：rbuf / frame_buf 下轮 recv 就被覆盖了。 */
                                struct frame_job *fj = malloc(sizeof *fj + flen);
                                if (fj == NULL) {
                                    printf("drop: 内存不足 fd=%d\n", c->fd);
                                } else {
                                    fj->slot = (int)(c - conns);
                                    fj->gen  = c->gen;
                                    fj->fd   = c->fd;
                                    fj->len  = flen;
                                    memcpy(fj->buf, frame_buf, flen);
                                    struct task tj = { frame_job_run, fj };
                                    if (pool_submit(&g_pool, (size_t)fj->slot, tj) != 0) {
                                        printf("drop: 任务队列满 fd=%d（整帧丢弃）\n", c->fd);
                                        free(fj);
                                    }
                                }
                            } else if (rc == -1) {
                                /* 超长帧：parser 已丢弃并重新同步，接着喂后面的字节 */
                                printf("drop: 超长帧 fd=%d\n", c->fd);
                            }
                        }
                        /* 这 m 个字节"一个不漏"都喂进去了 => 半帧的保管权已在 c->fp 手里，
                         * rbuf 不再需要留下任何字节。这一行必须写在 for 之后：
                         * 写在前头就等于把还没喂的字节扔了。 */
                        c->rlen = 0;
                        continue;
                    }

                    if (m == 0) {              // 对端正常关闭（FIN）
                        printf("conn_fd=%d closed total=%d\n", c->fd, c->total);
                        // 删除 —— 失败只记一行，流程不变（ENOENT 是正常的）
                        if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0 && errno != ENOENT)
                            perror("epoll_ctl DEL");

                        conn_free(c);
                        close(fd);
                        break;
                    }
                    // m < 0
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;                 // 读干净了，回 epoll_wait
                    }
                    if (errno == EINTR) {
                        continue;              // 被信号打断，重读
                    }
                    perror("recv");
                    // 删除 —— 失败只记一行，流程不变（ENOENT 是正常的）
                    if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL) < 0 && errno != ENOENT)
                        perror("epoll_ctl DEL");
                    conn_free(c);
                    close(fd);
                    break;
                }
            }
        }
    }

    close(g_efd);       /* eventfd 也是 fd，一起还 */
    close(listen_fd);
    close(epfd);        /* 簿子也要还给内核 */
    return 0;
}

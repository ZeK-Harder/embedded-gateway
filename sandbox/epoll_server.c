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
#include <sys/epoll.h>     // epoll_create1 / epoll_ctl / epoll_wait
#include <fcntl.h>         // fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include "proto.h"
#include <signal.h>

#define MAX_CONN 1024

struct conn {
    int fd;
    int total;          /* 这条连接累计读到多少字节 */
    int used;           /* 槽位是否被占用：0 = 空槽 */
    unsigned char rbuf[4096];   /* 这条连接攒下的原始字节（半成品柜） */
    size_t rlen;         /* 柜台里现在有多少字节 */
    frame_parser_t fp;   /* 这条连接自己的切包状态机：跨 recv 的半帧住在这里 */
};

static struct conn conns[MAX_CONN];
static volatile sig_atomic_t g_stop = 0;      /* 只允许是这一种类型 */
static void on_stop(int sig) { (void)sig; g_stop = 1; }   /* handler 里只写这一句 */

/* 从池子里拿一个空槽；拿不到返回 NULL */
static struct conn *conn_alloc(void)
{
    for (int i = 0; i < MAX_CONN; i++)
        if (!conns[i].used) {
            conns[i].used  = 1;
            conns[i].fd    = -1;
            conns[i].total = 0;
            conns[i].rlen  = 0;
            /* 清理放在唯一的"分配入口"，不放在"释放出口"：
             * 槽位要变成某条连接的只有这一条路，放这里物理上不可能漏；
             * 而关闭路径已经有 3 条（FIN / recv 错误 / 缓冲满），以后还会加心跳超时、写失败 --
             * 放出口就得每条都记得补一次。 */
            frame_parser_init(&conns[i].fp);
            return &conns[i];
        }
    return NULL;
}

static void conn_free(struct conn *c)
{
    c->used = 0;
}

int main(void) {

    setvbuf(stdout, NULL, _IOLBF, 0);   //输出重定向到文件时否则全缓冲，脚本读不到

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

            if (c == NULL) {            // 有人来连
                int conn_fd = accept(listen_fd, NULL, NULL);
                if (conn_fd < 0) {
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

            } else {                          // 已连接的客人可读
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
                                /* decode 的 4 个字段 + 1 块载荷空间，先全部给 0，
                                 * 保证 drc 失败时打印出来的是确定值而不是垃圾 */
                                uint8_t  dev  = 0;
                                uint8_t  func = 0;
                                uint16_t seq  = 0;
                                uint8_t  payload[PROTO_PAYLOAD_MAX];  /* V1 不解释载荷，但 API 要一块地方接 */
                                size_t   plen = 0;

                                /* drc 和上面的 rc 是两个东西：
                                 *   rc  = parser 交没交出一整帧（这里已经=1）
                                 *   drc = 这帧对不对（帧头帧尾 / LEN 自洽 / CRC） */
                                int drc = proto_decode(frame_buf, flen, &dev, &func,&seq, payload, sizeof payload, &plen);
                                if (drc == 0) {
                                    printf("frame dev=%u func=%u seq=%u plen=%zu\n",
                                           (unsigned)dev, (unsigned)func, (unsigned)seq, plen);
                                } else {
                                    /* 数据坏 != 通道坏：只丢这一帧，连接和后面的帧都要继续 */
                                    printf("drop: 解码失败 fd=%d\n", c->fd);
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

    close(listen_fd);
    close(epfd);        /* 簿子也要还给内核 */
    return 0;
}

/* loadgen —— 并发设备模拟器（压测器）· 阶段一 MVP
 *
 * 线程模型（学生 2026-10-31 拍板 B）：每条连接一对线程
 *   sender  ：按固定间隔用 proto_encode 造帧 → send 出去
 *   receiver：recv 回来 → 逐字节喂 frame_parser → 数"收齐了几帧"
 *
 * 一个刻意的设计：所有计数都挂在自己的 struct gen_conn 上。
 *   sent / recv / failed 只有本连接的那个线程写，主线程 join 完才读 ⇒ 全文件一把锁都不需要。
 *   回包必须读：不读的话服务端写缓冲（8192 字节）会憋满 → 整帧丢弃 → 你的"错误率"是被自己害的。
 *
 * 发送模式（-m，切片 2 学生拍板 A：粘包流量由压测器自己造）
 *   framewise（默认）：一帧一次 send —— 服务端每次 recv 都在帧边界上
 *   sticky          ：每轮一次 send 带"半帧 + 2 整帧 + 半帧"⇒ **首轮恰好 2.5 帧**，
 *                     剩下的半帧挂到下一轮才补齐。用来验证服务端 frame_parser 的切包/粘包能力。
 *
 * 验收脚本：verify/wsl-loadgen-skeleton-check.py（8 连接 × 2s × 50ms → 对账）
 *           verify/wsl-loadgen-sticky-check.py （1 连接 sticky → 2.5 帧只吐 2 帧）
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>          /* printf / snprintf / fprintf */
#include <stdint.h>         /* uint8_t / uint16_t */
#include <stdlib.h>         /* strtol */
#include <string.h>         /* memset / memcpy / strchr / strerror */
#include <unistd.h>         /* getopt / close */
#include <errno.h>          /* errno / EINTR / EAGAIN */
#include <signal.h>         /* sigaction / SIGINT */
#include <pthread.h>        /* pthread_create / pthread_join */
#include <time.h>           /* clock_gettime / clock_nanosleep */
#include <sys/time.h>       /* struct timeval（SO_RCVTIMEO 的 optval） */
#include <sys/socket.h>     /* socket / connect / send / recv / setsockopt */
#include <netinet/in.h>     /* struct sockaddr_in */
#include <arpa/inet.h>      /* inet_pton */

#include "proto.h"          /* proto_encode / frame_parser_* */

#define MAX_DEV        255                                  /* dev_id 只有 1 字节 */
#define FRAME_CAP      (2 * (1 + PROTO_LEN_FIXED + 1) + 2)  /* payload 1 字节 → 线上上限 18 */
#define RCV_TIMEOUT_MS 500    /* 收线程阻塞粒度和"收工后宽限"：停发后最多再等这么久没数据就退 */

struct gen_conn {
    int  idx;             /* 0 基：dev_id = idx + 1 */
    int  fd;
    int  interval_ms;
    int  sticky;          /* 1 = 每轮拼"半帧 + 2 整帧 + 半帧"再发（切片 2 加的） */
    uint16_t seq;         /* 这条连接自己的序号，从 0 递增 */
    unsigned long sent;   /* 只有本连接的 sender 写 */
    unsigned long recv;   /* 只有本连接的 receiver 写 */
    unsigned long failed;
    /* 下面三个只为 sticky 模式留证据（同样只有 sender 写） */
    unsigned long sends;       /* 一轮 = 一次 send */
    unsigned long half_sends;  /* 其中"劈开了一帧"的 send 次数 */
    size_t        max_send;    /* 单次 send 的最大字节数 */
    size_t        frame_len;   /* 单帧线上长度（造完第一帧就固定下来，给脚本当基准） */
};

static volatile sig_atomic_t g_stop = 0;   /* 时长到了 / 收到 SIGINT：所有线程的刹车 */

static void on_stop(int sig) { (void)sig; g_stop = 1; }

/* 字符串转整数并查范围；越界 / 不是数字返回 -1 */
static int parse_int(const char *s, long lo, long hi, long *out)
{
    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < lo || v > hi)
        return -1;
    *out = v;
    return 0;
}

/* 把 len 个字节发完（阻塞 socket 下 send 也可能只发一半） */
static int send_all(int fd, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, buf + off, len - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            if (g_stop) return -1;
            continue;
        }
        return -1;         /* EPIPE / ECONNRESET / EBADF … */
    }
    return 0;
}

/* 睡到 deadline（绝对时间）。用"累加时间点"而不是"每次睡 interval"：
 * 后者会把每帧 send 的耗时也累加进去，跑久了实际频率越来越慢。 */
static void sleep_until(struct timespec *deadline, int interval_ms)
{
    deadline->tv_nsec += (long)interval_ms * 1000000L;
    while (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_nsec -= 1000000000L;
        deadline->tv_sec  += 1;
    }
    /* 被信号打断：绝对时间点没变，重试就行（g_stop 了就退出循环） */
    while (!g_stop && clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, deadline, NULL) == EINTR) {
        /* 空循环体：上面的条件就是全部逻辑，花括号只为别让读的人以为漏了语句 */
    }
}

static void *sender_main(void *arg)
{
    struct gen_conn *c = arg;
    uint8_t  payload[1];
    uint8_t  out[4 * FRAME_CAP];    /* sticky：残料(≤1 帧) + 2 整帧 + 1 半帧，留足余量 */
    uint8_t  rest[FRAME_CAP];       /* sticky：上一轮劈开后剩下的半帧 */
    size_t   rlen = 0;
    struct timespec next;

    clock_gettime(CLOCK_MONOTONIC, &next);

    while (!g_stop) {
        if (!c->sticky) {
            /* ---------- framewise：一帧一次 send（切片 1 的行为，默认） ---------- */
            payload[0] = (uint8_t)(c->seq & 0xFF);
            size_t n = proto_encode((uint8_t)(c->idx + 1), 1, c->seq,
                                    payload, sizeof payload, out, sizeof out);
            if (n == 0) {                           /* 参数 / 容量错：压测器自己坏了 */
                c->failed++;
                break;
            }
            if (send_all(c->fd, out, n) < 0) {      /* 通道坏了：这条连接不再发 */
                c->failed++;
                break;
            }
            c->seq++;
            c->sent++;
        } else {
            /* ---------- sticky：一次 send = 残料 + 2 整帧 + 半帧 ---------- */
            size_t len = 0;
            unsigned long whole = 0;   /* 本轮字节流里"完整出现"的帧数 */
            int half = 0;              /* 本轮有没有劈开一帧 */

            if (rlen > 0) {            /* 上一轮劈开的半帧接在最前面，本轮补齐它 */
                memcpy(out, rest, rlen);
                len  = rlen;
                whole = 1;
                rlen = 0;
            }

            for (int k = 0; k < 3; k++) {
                payload[0] = (uint8_t)(c->seq & 0xFF);
                size_t n = proto_encode((uint8_t)(c->idx + 1), 1, c->seq,
                                        payload, sizeof payload,
                                        out + len, sizeof out - len);
                if (n == 0) {
                    c->failed++;
                    return NULL;
                }
                c->seq++;
                if (c->frame_len == 0) c->frame_len = n;   /* 给脚本当"一帧多长"的基准 */
                if (k < 2) {                   /* 前两帧整帧进缓冲 */
                    len += n;
                    whole++;
                } else {                       /* 第三帧只带前半截，后半截留下轮补 */
                    size_t h = n / 2;
                    len += h;
                    rlen = n - h;
                    memcpy(rest, out + len, rlen);
                    half = 1;
                }
            }

            if (send_all(c->fd, out, len) < 0) {
                c->failed++;
                return NULL;
            }
            c->sent += whole;                  /* 只数"整帧已经全发出去"的 */
            c->sends++;
            if (half) c->half_sends++;
            if (len > c->max_send) c->max_send = len;
        }
        sleep_until(&next, c->interval_ms);
    }

    /* sticky 收尾：最后一轮劈开的那半帧补完，服务端才会把它算成一帧（否则账差 1） */
    if (c->sticky && rlen > 0) {
        if (send_all(c->fd, rest, rlen) == 0)
            c->sent++;
        else
            c->failed++;
    }
    return NULL;
}

static void *receiver_main(void *arg)
{
    struct gen_conn *c = arg;
    uint8_t buf[4096];
    uint8_t frame[PROTO_FRAME_WIRE_MAX];
    frame_parser_t fp;

    frame_parser_init(&fp);

    for (;;) {
        ssize_t m = recv(c->fd, buf, sizeof buf, 0);

        if (m > 0) {
            for (ssize_t i = 0; i < m; i++) {
                size_t flen = 0;
                if (frame_parser_feed(&fp, buf[i], frame, sizeof frame, &flen) == 1)
                    c->recv++;
            }
            continue;
        }
        if (m == 0) break;                              /* 服务端关了 */
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {  /* SO_RCVTIMEO 超时 */
            /* g_stop 之后的第一个超时 = 已经读空了（还有数据的话上面 m>0 分支会继续） */
            if (g_stop) break;
            continue;
        }
        break;                                          /* 真错 */
    }
    return NULL;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "用法: %s [-c 并发连接数 1..255] [-t 秒] [-i 帧间隔ms] [-h host[:port]] [-p port]\n"
            "          [-m framewise|sticky]\n"
            "  默认: -c 1 -t 5 -i 100 -h 127.0.0.1 -p 8090 -m framewise\n"
            "  -m sticky：每轮一次 send 带\"半帧 + 2 整帧 + 半帧\"，首轮恰好 2.5 帧（验证切包）\n",
            prog);
}

int main(int argc, char **argv)
{
    long nconn    = 1;
    long secs     = 5;
    long interval = 100;
    long port     = 8090;
    int  sticky   = 0;                  /* 0 = framewise（默认，一帧一次 send） */
    char host[64] = "127.0.0.1";

    int opt;
    while ((opt = getopt(argc, argv, "c:t:i:h:p:m:")) != -1) {
        switch (opt) {
        case 'c':
            if (parse_int(optarg, 1, MAX_DEV, &nconn) != 0) {
                fprintf(stderr, "loadgen: -c 只接受 1..%d（dev_id 是 1 字节）\n", MAX_DEV);
                return 2;
            }
            break;
        case 't':
            if (parse_int(optarg, 1, 3600, &secs) != 0) {
                fprintf(stderr, "loadgen: -t 只接受 1..3600 秒\n");
                return 2;
            }
            break;
        case 'i':
            if (parse_int(optarg, 1, 60000, &interval) != 0) {
                fprintf(stderr, "loadgen: -i 只接受 1..60000 ms\n");
                return 2;
            }
            break;
        case 'p':
            if (parse_int(optarg, 1, 65535, &port) != 0) {
                fprintf(stderr, "loadgen: -p 只接受 1..65535\n");
                return 2;
            }
            break;
        case 'h': {
            const char *colon = strchr(optarg, ':');
            if (colon != NULL) {                        /* -h 127.0.0.1:8090 这种写法 */
                size_t hl = (size_t)(colon - optarg);
                if (hl == 0 || hl >= sizeof host) {
                    fprintf(stderr, "loadgen: -h 的 host 部分为空或太长\n");
                    return 2;
                }
                memcpy(host, optarg, hl);
                host[hl] = '\0';
                if (parse_int(colon + 1, 1, 65535, &port) != 0) {
                    fprintf(stderr, "loadgen: -h host:port 的端口非法\n");
                    return 2;
                }
            } else {
                if (strlen(optarg) >= sizeof host) {
                    fprintf(stderr, "loadgen: -h 太长\n");
                    return 2;
                }
                snprintf(host, sizeof host, "%s", optarg);
            }
            break;
        }
        case 'm':
            if (strcmp(optarg, "framewise") == 0) {
                sticky = 0;
            } else if (strcmp(optarg, "sticky") == 0) {
                sticky = 1;
            } else {
                fprintf(stderr, "loadgen: -m 只接受 framewise / sticky（给的是 %s）\n", optarg);
                return 2;
            }
            break;
        default:
            usage(argv[0]);
            return 2;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "loadgen: -h 只支持 IPv4 点分格式（不做域名解析）：%s\n", host);
        return 2;
    }

    static struct gen_conn conns[MAX_DEV];
    memset(conns, 0, sizeof conns);

    int connected = 0;
    for (int i = 0; i < (int)nconn; i++) {
        conns[i].idx         = i;
        conns[i].fd          = -1;
        conns[i].interval_ms = (int)interval;
        conns[i].sticky      = sticky;

        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            perror("socket");
            break;
        }
        if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
            fprintf(stderr, "loadgen: 第 %d 条连接失败：%s\n", i, strerror(errno));
            close(fd);
            continue;
        }
        struct timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = RCV_TIMEOUT_MS * 1000;
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        /* 万一服务端一直不回：别让 sender 永久卡在 send 里 */
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

        conns[i].fd = fd;
        connected++;
    }

    printf("loadgen: connected=%d/%ld c=%ld t=%lds i=%ldms target=%s:%ld mode=%s\n",
           connected, nconn, nconn, secs, interval, host, port,
           sticky ? "sticky" : "framewise");

    static pthread_t th_send[MAX_DEV];
    static pthread_t th_recv[MAX_DEV];
    static int started_send[MAX_DEV];
    static int started_recv[MAX_DEV];

    /* 先起收线程再起发线程：第一帧发出去时就有人在读回包 */
    for (int j = 0; j < (int)nconn; j++) {
        if (conns[j].fd < 0) continue;
        if (pthread_create(&th_recv[j], NULL, receiver_main, &conns[j]) == 0) {
            started_recv[j] = 1;
        } else {
            fprintf(stderr, "loadgen: 第 %d 条收线程起不来\n", j);
            conns[j].failed++;
        }
    }
    for (int k = 0; k < (int)nconn; k++) {
        if (conns[k].fd < 0) continue;
        if (pthread_create(&th_send[k], NULL, sender_main, &conns[k]) == 0) {
            started_send[k] = 1;
        } else {
            fprintf(stderr, "loadgen: 第 %d 条发线程起不来\n", k);
            conns[k].failed++;
        }
    }

    struct timespec t0;
    struct timespec tn;
    struct timespec tick = { 0, 100 * 1000000L };   /* 主线程每 100ms 看一眼到时间没 */

    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (!g_stop) {
        clock_gettime(CLOCK_MONOTONIC, &tn);
        double el = (double)(tn.tv_sec - t0.tv_sec) + (double)(tn.tv_nsec - t0.tv_nsec) / 1e9;
        if (el >= (double)secs) break;
        nanosleep(&tick, NULL);
    }
    g_stop = 1;                                    /* 刹车：发线程停，收线程把剩下的读完再退 */

    for (int m = 0; m < (int)nconn; m++) if (started_send[m]) pthread_join(th_send[m], NULL);
    for (int n = 0; n < (int)nconn; n++) if (started_recv[n]) pthread_join(th_recv[n], NULL);

    clock_gettime(CLOCK_MONOTONIC, &tn);
    double elapsed = (double)(tn.tv_sec - t0.tv_sec) + (double)(tn.tv_nsec - t0.tv_nsec) / 1e9;

    unsigned long tot_sent = 0;
    unsigned long tot_recv = 0;
    unsigned long tot_fail = 0;
    for (int q = 0; q < (int)nconn; q++) {
        tot_sent += conns[q].sent;
        tot_recv += conns[q].recv;
        tot_fail += conns[q].failed;
    }

    double qps = (elapsed > 0.0) ? (double)tot_sent / elapsed : 0.0;
    printf("loadgen: sent=%lu recv=%lu failed=%lu elapsed=%.2fs qps_sent=%.1f\n",
           tot_sent, tot_recv, tot_fail, elapsed, qps);

    /* sticky 模式的自我举证：脚本要靠这三行数字判断"真的劈开过帧" */
    if (sticky) {
        unsigned long sends = 0;
        unsigned long halves = 0;
        size_t mx = 0;
        size_t fl = 0;
        for (int q = 0; q < (int)nconn; q++) {
            sends  += conns[q].sends;
            halves += conns[q].half_sends;
            if (conns[q].max_send > mx) mx = conns[q].max_send;
            if (conns[q].frame_len > 0) fl = conns[q].frame_len;
        }
        printf("loadgen: sticky sends=%lu half_sends=%lu max_send=%zu frame_len=%zu\n",
               sends, halves, mx, fl);
    }

    for (int r = 0; r < (int)nconn; r++) if (conns[r].fd >= 0) close(conns[r].fd);

    if (connected != (int)nconn) {
        fprintf(stderr, "loadgen: 有连接没建上（%d/%ld）\n", connected, nconn);
        return 1;
    }
    if (tot_fail != 0) {
        fprintf(stderr, "loadgen: 有 %lu 次发送失败\n", tot_fail);
        return 1;
    }
    return 0;
}

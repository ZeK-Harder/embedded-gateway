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

#define MAX_CONN 1024      // total 数组容量，下标上限

int main(void) {
    // ---------- 造门 ----------
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    } else {
        printf("socket 返回 %d\n", listen_fd);
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
    ev.data.fd  = listen_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);

    // 累计字节数按 fd 存，放在循环外，跨多次事件不归零
    int total[MAX_CONN] = {0};

    // ---------- 事件循环 ----------
    struct epoll_event evs[8];
    while (1) {
        int n = epoll_wait(epfd, evs, 8, -1);
        if (n < 0) {
            if (errno == EINTR) continue;   // 被信号打断，重来
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            int fd = evs[i].data.fd;

            if (fd == listen_fd) {            // 有人来连
                int conn_fd = accept(listen_fd, NULL, NULL);
                if (conn_fd < 0) {
                    perror("accept");
                    continue;
                }

                // 边界检查：fd 来自外部，超出 total 容量就不登记，直接关掉
                if (conn_fd >= MAX_CONN) {
                    printf("conn_fd=%d 超出容量 %d，关闭\n", conn_fd, MAX_CONN);
                    close(conn_fd);
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
                total[conn_fd] = 0;           // 新连接进来，累计先清零（已在界内）

                ev.events  = EPOLLIN;
                ev.data.fd  = conn_fd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, conn_fd, &ev);
                printf("已登记 conn_fd=%d\n", conn_fd);

            } else {                          // 已连接的客人可读
                char buf[1024];
                ssize_t m;
                while (1) {
                    m = recv(fd, buf, sizeof buf, 0);
                    if (m > 0) {               // 有数据
                        total[fd] += (int)m;  // 累加（已在界内）
                        printf("conn_fd=%d 读到 %zd 字节，累计 %d: %.*s\n",
                               fd, m, total[fd], (int)m, buf);
                        continue;
                    }
                    if (m == 0) {              // 对端正常关闭（FIN）
                        printf("conn_fd=%d 断开了，累计 %d 字节\n", fd, total[fd]);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
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
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    break;
                }
            }
        }
    }

    close(listen_fd);
    return 0;
}

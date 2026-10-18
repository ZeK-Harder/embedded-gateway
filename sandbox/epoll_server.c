// ---------- 头文件（一行一个来源）----------
#include <stdio.h>       // printf
#include <string.h>      // memset
#include <unistd.h>      // read / close / sleep
#include <sys/socket.h>  // socket / bind / listen / accept
#include <netinet/in.h>  // struct sockaddr_in
#include <arpa/inet.h>   // htons / htonl / inet_addr
#include <errno.h>       // errno
#include <stdlib.h>      // exit / EXIT_FAILURE
#include <sys/epoll.h>   // epoll_create1 / epoll_ctl / epoll_wait / struct epoll_event

int main(void) {
    // ---------- 造门 ----------
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);   // 造一个 TCP 端点；成功→fd（屏幕上 3）
    if (listen_fd < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }   // 失败就当场死（启动阶段）
    else{
        printf("socket 返回 %d\n", listen_fd);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));       // 清零，把 sin_zero 那 8 个填充字节清干净
    addr.sin_family = AF_INET;            // IPv4
    addr.sin_port   = htons(8090);        // 端口：必须转成网络字节序
    addr.sin_addr.s_addr = htonl(INADDR_ANY);   // 接所有网卡

    int r = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));  // 把这个端点"钉"在 8090 上
    if (r < 0){
        perror("bind"); exit(EXIT_FAILURE);
    }   // 端口被占就死（你实测过退出码 1）
    else{
        printf("bind 返回 %d\n", r);                    // 成功 = 0
    }

    int r2 = listen(listen_fd, 8);        // 从"端点"变成"门"：开始接受连接，排队上限 8
    if (r2 < 0) {
        perror("listen"); exit(EXIT_FAILURE);
    }
    else{
        printf("listen 返回 %d\n", r2);
    }

    //char buf[1024];                     // 现在还没人读数据，先注释掉，等下一步用

    // ---------- 造簿子 + 把门登记进去 ----------
    int epfd = epoll_create1(0);          // 向内核要一本登记簿；它也是 fd（这次是 4）
    struct epoll_event ev;                // 一张"登记单"
    ev.events = EPOLLIN;                  // 我关心：门"可读"时叫我（门可读 = 有新人来排队）
    ev.data.fd = listen_fd;               // 便条：这条登记对应门（3）—— 内核不看，原样还你
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);   // 报备：把这单子交给内核

    // ---------- 事件循环 ----------
    struct epoll_event evs[8];            // 结果表：内核把"就绪的"填进来
    while (1) {
        int n = epoll_wait(epfd, evs, 8, -1);   // 睡在这等电话；-1 = 一直等，不超时
        for (int i = 0; i < n; i++) {           // 只处理前 n 条（后面的格子是旧数据）
            if (evs[i].data.fd == listen_fd) {  // 小票上的号 == 门 → 说明"有人来连"
                int conn_fd = accept(listen_fd, NULL, NULL);   // 把客人请进来，拿到他专用的通道 fd
                printf("新连接 conn_fd=%d\n", conn_fd);         // 屏幕上那行 5
                close(conn_fd);                 // 这一步先立刻还回去（不读数据）
            }
        }
    }

    // ---------- 下面这两句永远执行不到（死代码）----------
    printf("在监听 8090\n");      // 因为上面 while(1) 不会退出
    close(listen_fd);            // 位置本身是对的：门在程序结束时才关；等以后加了 Ctrl+C 处理才会走到
}
#include <sys/epoll.h>
#include <stdio.h>
#include <unistd.h>

int main(){
    int epfd = epoll_create1(0);
    printf("%d\n",epfd);
    
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = 0;

    epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &ev);/* op：EPOLL_CTL_ADD（加入）/ EPOLL_CTL_MOD（改）/ EPOLL_CTL_DEL（移除）*/

    struct epoll_event evs[8];
    while(1){
        int n = epoll_wait(epfd, evs, 8, 3000);
        printf("epoll_wait 返回 %d\n", n);
        for (int i = 0; i < n; i++) {
            printf("  就绪 fd = %d  events = 0x%x\n", evs[i].data.fd, evs[i].events);
            char buf[64];
            ssize_t m = read(evs[i].data.fd, buf, sizeof buf);   /* 注意是 ssize_t，不是 int */
            if (m > 0) printf("  读到 %zd 字节: %.*s", m, (int)m, buf);
        }
    }
}    
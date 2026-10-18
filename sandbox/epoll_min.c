#include <sys/epoll.h>   // epoll_create1 / epoll_ctl / epoll_wait / struct epoll_event 都在这
#include <stdio.h>       // printf
#include <unistd.h>      // read（还有 close）

int main(){
    // ① 造簿子：向内核要一个 epoll 实例，返回一个 fd
    int epfd = epoll_create1(0);
    printf("%d\n", epfd);          // 屏幕上第一行那个 3 = 这个"服务号"，后面所有操作都报它的号

    // ② 填单子：说清"我关心什么"，并写一张自己的便条
    struct epoll_event ev;
    ev.events = EPOLLIN;           // 关心"可读"（有人说话）。屏幕上看不到它，但它决定 wait 会不会返回 1
    ev.data.fd = 0;                // 便条：这条对应 fd 0（键盘）。内核不核对内容，只在结果里原样还给你

    // ③ 报备：把单子交给内核（ADD=登记）。成功 0 / 失败 -1
    //    注意：这行你没检查返回值 —— 刚才把它注释掉，程序一声不响地跑了 10 秒"返回 0"
    epoll_ctl(epfd, EPOLL_CTL_ADD, 0, &ev);

    // ④ 准备结果表：内核把"就绪的条目"填进来，一次最多 8 条
    struct epoll_event evs[8];

    while (1) {
        // ⑤ 睡在这里等，最多 3 秒。返回 n = 就绪条数：
        //    n>0 有事件 / n==0 这 3 秒没人找我（正常）/ n<0 出错
        int n = epoll_wait(epfd, evs, 8, 3000);
        printf("epoll_wait 返回 %d\n", n);        // ← 屏幕上那行 0 或 1 就是它

        // ⑥ 只处理前 n 项：后面的格子是旧数据，不能看
        for (int i = 0; i < n; i++) {
            // 便条原样还回来（fd 编号）+ 内核告诉你"怎么了"：
            //   0x1 = 可读 / 0x10 = 对方挂了 / 0x11 = 两件事一起
            printf("  就绪 fd = %d  events = 0x%x\n", evs[i].data.fd, evs[i].events);

            char buf[64];                                   // 接听用的本子
            // ⑦ 真的去接听。返回读到的字节数（"123"+回车 = 4 字节）；0 = 对端关了；-1 = 出错
            ssize_t m = read(evs[i].data.fd, buf, sizeof buf);
            if (m > 0) printf("  读到 %zd 字节: %.*s", m, (int)m, buf);   // 只打印这 m 个字节，不碰本子后面的垃圾
        }
    }
}

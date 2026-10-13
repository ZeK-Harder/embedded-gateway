#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>

int main(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);       // 成功→fd(≥3)；失败→-1
    printf("socket 返回 %d\n", fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));                 // 先清零，别跳过
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(8090);                  // 端口必须 htons
    addr.sin_addr.s_addr = htonl(INADDR_ANY);       // 或 inet_addr("127.0.0.1")

    int r = bind(fd, (struct sockaddr *)&addr, sizeof(addr));   // 成功→0
    if(r<0){
        perror("bind");
        exit(EXIT_FAILURE);
    }
    else{
        printf("bind 返回 %d\n", r);
    }

    int r2 = listen(fd, 8);                                     // 成功→0
    printf("listen 返回 %d\n", r2);
    char buf[1024];

    while(1){
        int cfd = accept(fd, NULL, NULL);
        printf("accept 返回 %d\n", cfd);

        int n = recv(cfd, buf, sizeof(buf), 0);
        if(n>0){
            printf("%.*s\n", n, buf);
        }

        close(cfd);
    }
    
    printf("在监听 8090\n");
    close(fd);
}


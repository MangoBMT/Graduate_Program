#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#define PORT 32399
#define QUEUE 10

typedef struct
{
    struct sockaddr_in addr;
    socklen_t addr_len;
    int connectfd;
} thread_args;

void handle_thread(void *arg)
{
    thread_args *targs = (thread_args *)arg;
    pthread_t tid = pthread_self();
    printf("tid = %u and socket = %d\n", tid, targs->connectfd);
    char send_buf[BUFSIZ] = {0}, recv_buf[BUFSIZ] = {0}; // 定义接受和发送缓冲区
    while (1)
    {
        int len = recv(targs->connectfd, recv_buf, BUFSIZ, 0); // 读取数据
        printf("[Client %d] %s", targs->connectfd, recv_buf);

        if (strcmp("q\n", recv_buf) == 0)
            break;

        sprintf(send_buf, pattern, recv_buf);                  //  将字符串pattern 和 recv_buf 合并
        send(targs->connectfd, send_buf, strlen(send_buf), 0); // 发送数据

        memset(send_buf, 0, BUFSIZ), memset(recv_buf, 0, BUFSIZ); // 清空缓存
    }
    close(targs->connectfd);
    free(targs);
    pthread_exit(NULL);
}

int main()
{
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    printf("server is listening at socket fd = %d\n", listenfd);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("bind error\n");
        exit(-1);
    }

    if (listen(listenfd, QUEUE) == -1)
    {
        perror("listen error\n");
        exit(-1);
    }

    while (1) // 开启循环
    {
        thread_args *targs = malloc(sizeof(thread_args));
        targs->connectfd = accept(listenfd, (struct sockaddr *)&targs->addr, &targs->addr_len); 
        // int newfd = accept(sockfd, NULL, NULL);
        pthread_t tid;
        pthread_create(&tid, NULL, handle_thread, (void *)targs); // 创建线程

        /*线程是joinable状态，当线程函数自己返回退出时或pthread_exit时都不会释放线程所占用堆栈和线程描述符（总计8K多）。只有当你调用了pthread_join之后这些资源才会被释放。若是unjoinable状态的线程，这些资源在线程函数退出时或pthread_exit时自动会被释放。
*/
        pthread_detach(tid); //
    }
    close(listenfd);
}
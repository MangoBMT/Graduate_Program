#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <string.h>

#include "../elastic/ElasticSketch.h"

#define SERV_PORT 32399
#define QUEUE 10
#define HEAVY_MEM (136 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 136)
#define TOT_MEM_IN_BYTES (512 * 1024)

typedef struct
{
    struct sockaddr_in addr;
    socklen_t addr_len;
    int connectfd;
} thread_args;

void handle_thread()
{
    ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
    thread_args *targs = (thread_args *)arg;
    char buf[TOT_MEM_IN_BYTES] = "";
    while (true)
    {
        int len = recv(targs->connectfd, buf, TOT_MEM_IN_BYTES, 0);
        if (len == TOT_MEM_IN_BYTES){
            memcpy(elastic, buf, sizeof(buf));
        }
    }
    close(targs->connectfd);
    free(targs);
    pthread_exit(NULL);
}

int main()
{
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERV_PORT);
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        perror("bind error\n");
        exit(0);
    }
    if (listen(listenfd, QUEUE) == -1)
    {
        perror("listen error\n");
        exit(0);
    }

    while (true)
    {
        thread_args *targs = malloc(sizeof(thread_args));
        targs->connectfd = accept(listenfd, (struct sockaddr *)&targs->addr, &targs->addr_len); 
        thread getSketch(handle_thread, targs);
        getSketch.detach();
    }
    close(listenfd);
}
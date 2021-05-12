#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../elastic/ElasticSketch.h"
using namespace std;

#define PORT 32399
#define QUEUE 10
#define HEAVY_MEM (128 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 128)
#define TOT_MEM_IN_BYTES (512 * 1024)

#define SERV_PORT 5000
#define SERV_IP "127.0.0.1"

typedef struct
{
    struct sockaddr_in addr;
    socklen_t addr_len;
    int connectfd;
} thread_args;
typedef vector< ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>* > SketchRepository;
SketchRepository sketches;

void sendMsg()
{
    thread_args *targs = (thread_args *)malloc(sizeof(thread_args));
    buf = (char *)malloc(TOT_MEM_IN_BYTES * sizeof(char));

    targs->connectfd = socket(AF_INET, SOCK_STREAM, 0);
    targs->addr.sin_family = AF_INET;
    targs->addr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, SERV_IP, &serv_addr.sin_addr.s_addr);

    connect(targs->connectfd, (struct sockaddr *)&targs->addr, sizeof(targs->serv_addr));
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(1));
        mtx.lock();
        memcpy(buf, elastic, TOT_MEM_IN_BYTES);
        elastic->heavy_part.clear();
        elastic->light_part.clear();
        mtx.unlock();
        send(cfd, buf, TOT_MEM_IN_BYTES, 0);
    }
    close(cfd);
    free(buf);
}

void recieveSketch(thread_args *targs, SketchRepository::iterator &it)
{
    char buf[TOT_MEM_IN_BYTES];
    while (true)
    {
        int len = recv(targs->connectfd, buf, TOT_MEM_IN_BYTES, 0);
        if (len == TOT_MEM_IN_BYTES){
            mtx.lock();
            memcpy((*it), buf, sizeof(buf));
            mtx.unlock();
        }
    }
    close(targs->connectfd);
    free(targs);
}

int main()
{

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
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
        thread_args *targs = (thread_args *)malloc(sizeof(thread_args));
        targs->connectfd = accept(listenfd, (struct sockaddr *)&targs->addr, &targs->addr_len);
        ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
        sketcher.push_back(elastic);
        SketchRepository::iterator it = sketches.end() - 1;
        thread getSketch(recieveSketch, targs, it);
        getSketch.detach();
    }
    close(listenfd);
}
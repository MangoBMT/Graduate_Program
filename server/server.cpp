#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "../elastic/ElasticSketch.h"
using namespace std;

#define PORT 32399
#define QUEUE 10
#define MAX_LENGTH 1048575
#define HEAVY_MEM (128 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 128)
#define TOT_MEM_IN_BYTES (512 * 1024)

#define SERV_PORT 5001
#define SERV_IP "10.128.189.187"

typedef struct
{
    struct sockaddr_in addr;
    socklen_t addr_len;
    int connectfd;
} thread_args;
typedef struct
{
    uint32_t srcIP;
    uint32_t dstIP;
    uint16_t srcPort;
    uint16_t dstPort;
    uint8_t protocal;
} FIVE_TUPLE;

typedef vector<ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *> SketchRepository;
SketchRepository sketches;
mutex mtx;
int id;

/* Json Structure
{
    "id": int,
    "timestamp": string,
    "algorithm": string,
    "cardinality": int,
    "entropy": double, 
    "heavy_part": list of 5-tuples + flow size + swap flag,
    "distribution": list of int,
}
*/

void makeJSON(char *json)
{
    ++id;
    int cardinality = 0;
    double entropy;
    vector<int> dist;
    vector<pair<FIVE_TUPLE, int> > heavy_part;
    int tot;
    double entr;
    time_t cur_time = time(0);
    for (SketchRepository::iterator it = sketches.begin(); it != sketches.end(); ++it){
        ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
        mtx.lock();
        memcpy(elastic, (*it), TOT_MEM_IN_BYTES);
        mtx.unlock();

        vector<int> tmp_dist;
        elastic->get_distribution(tmp_dist);
        if (tmp_dist.size() > dist.size()){
            dist.resize(tmp_dist.size());
        }
        for (int i = 0; i < tmp_dist.size(); ++i){
            dist[i] += tmp_dist[i];
        }

        vector<pair<string, int> > tmp_heavy_part;
        elastic->get_heavy_hitters(0, tmp_heavy_part);
        for (int i = 0; i < tmp_heavy_part.size(); ++i){
            FIVE_TUPLE *quintet = (FIVE_TUPLE *)malloc(KEY_LENGTH_13 * sizeof(char));
            memcpy(quintet, &(tmp_heavy_part[i].first), KEY_LENGTH_13);
            heavy_part.push_back(make_pair((*quintet), tmp_heavy_part[i].second));
        }

        cardinality += elastic->get_cardinality();

        for (int i = 1; i < 256; i++)
            tot += elastic->light_part.mice_dist[i] * i;
        for (int i = 0; i < BUCKET_NUM; ++i){
            for (int j = 0; j < MAX_VALID_COUNTER; ++j){
                char key[KEY_LENGTH_13];
                strncmp(key, elastic->heavy_part.buckets[i].key[j], KEY_LENGTH_13);
                int val = elastic->heavy_part.buckets[i].val[j];
                int ex_val = elastic->light_part.query(key);
                if (HIGHEST_BIT_IS_1(val) && ex_val)
                {
                    val += ex_val;
                    tot -= ex_val;
                    entr -= ex_val * log2(ex_val);
                }
                val = GetCounterVal(val);
                if (val)
                {
                    tot += val;
                    entr += val * log2(val);
                }
            }
        }
    }
    entropy = -entr / tot + log2(tot);

    sprintf(json, "{");
    sprintf(json, "\"ID\":%d,", id);
    sprintf(json, "\"Algorithm\":\"elastic\",");
    sprintf(json, "\"time\":%s,", ctime(&cur_time));
    sprintf(json, "\"cardinality\":%d,", cardinality);
    sprintf(json, "\"cardinality\":%lf,", entropy);
    sprintf(json, "\"heavy part\":[");
    for (int i = 0; i < heavy_part.size(); ++i){
        FIVE_TUPLE quintet = heavy_part[i].first;
        int f = heavy_part[i].second;
        sprintf(json, "[%u,%u,%d,%d,%d,%d],", quintet.srcIP, quintet.dstIP, quintet.srcPort, quintet.dstPort, quintet.protocal, f);
    }
    sprintf(json, "],");
    sprintf(json, "\"distribution\":[");
    for (int i = 1; i < dist.size(); ++i){
        if (dist[i] > 0){
            sprintf(json, "[%d,%d],", i, dist[i]);
        }
    }
    sprintf(json, "]");
    sprintf(json, "}\r\n");
}

void sendMsg()
{
    thread_args *targs = (thread_args *)malloc(sizeof(thread_args));
    char *buf = (char *)malloc(MAX_LENGTH * sizeof(char));

    targs->connectfd = socket(AF_INET, SOCK_STREAM, 0);
    targs->addr.sin_family = AF_INET;
    targs->addr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, SERV_IP, &targs->addr.sin_addr.s_addr);
    connect(targs->connectfd, (struct sockaddr *)&targs->addr, sizeof(targs->addr));
    printf("The connection to %s:%d has established.\n", SERV_IP, SERV_PORT);
    
    char header[1023];
    memset(header, 0, 1023);
    strcat(header, "POST /post HTTP/1.1\r\n");
    sprintf(header, "Host: ");
    sprintf(header, "%s:%d\r\n", SERV_IP, SERV_PORT);
    strcat(header, "Content-Type: application/json\r\n");
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(5));
        char json[MAX_LENGTH];
        memset(json, 0, MAX_LENGTH * sizeof(char));
        memset(buf, 0, MAX_LENGTH * sizeof(char));
        strcat(buf, header);
        makeJSON(json);
        int len = strlen(json);
        strcat(buf, "Content-Length: ");
        sprintf(buf, "%d\r\n", len);
        strcat(buf, "\r\n");
        strcat(buf, json);
        strcat(buf, "\r\n");
        send(targs->connectfd, buf, strlen(buf), 0);
        printf("Sketch updated.\n");
    }
    close(targs->connectfd);
    free(buf);
    free(targs);
}

void recieveSketch(thread_args *targs, SketchRepository::iterator it)
{
    char buf[TOT_MEM_IN_BYTES];
    while (true)
    {
        int len = recv(targs->connectfd, buf, TOT_MEM_IN_BYTES, 0);
        if (len == TOT_MEM_IN_BYTES)
        {
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
    thread monitor(sendMsg);
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

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
        sketches.push_back(elastic);
        SketchRepository::iterator it = sketches.end() - 1;
        thread getSketch(recieveSketch, targs, it);
        getSketch.detach();
    }
    close(listenfd);
    monitor.join();
}

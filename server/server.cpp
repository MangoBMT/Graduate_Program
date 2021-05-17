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
    "heavy_part": list of 5-tuples + flow size,
    "distribution": list of int,
}
*/

void makeJSON(char *json)
{
    ++id;
    int cardinality = 0;
    double entropy;
    vector<vector<int> > dist;
    vector<pair<FIVE_TUPLE, int>> heavy_part;
    int tot;
    double entr;

    time_t raw_time;
    struct tm timeinfo;
    time(&raw_time);
    timeinfo = *(localtime(&raw_time));

    for (SketchRepository::iterator it = sketches.begin(); it != sketches.end(); ++it)
    {
        ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic = (ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *)calloc(sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>), 1);
        mtx.lock();
        if ((*it) == NULL){
            free(elastic);
            continue;
        }
        memcpy(elastic, (*it), sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
        printf("%x, %x\n", elastic, (*it));
        fflush(stdout);
        mtx.unlock();
        vector<int> tmp_dist;
        elastic->get_distribution(tmp_dist);
        dist.push_back(tmp_dist);

        vector<pair<string, int>> tmp_heavy_part;
        elastic->get_heavy_hitters(0, tmp_heavy_part);
        for (int i = 0; i < tmp_heavy_part.size(); ++i)
        {
            FIVE_TUPLE *quintet = (FIVE_TUPLE *)malloc(KEY_LENGTH_13 * sizeof(char));
            memcpy(quintet, &(tmp_heavy_part[i].first), KEY_LENGTH_13);
            heavy_part.push_back(make_pair((*quintet), tmp_heavy_part[i].second));
            free(quintet);
        }

        cardinality += elastic->get_cardinality();

        for (int i = 1; i < 256; i++)
            tot += elastic->light_part.mice_dist[i] * i;
        for (int i = 0; i < BUCKET_NUM; ++i)
        {
            for (int j = 0; j < MAX_VALID_COUNTER; ++j)
            {
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
        free(elastic);
    }
    entropy = -entr / tot + log2(tot);

    int offset = 0;
    offset += sprintf(json + offset, "{");
    offset += sprintf(json + offset, "\"ID\":%d,", id);
    offset += sprintf(json + offset, "\"Algorithm\":\"elastic\",");
    offset += sprintf(json + offset, "\"Time\":\"%s", asctime(&timeinfo));
    --offset;
    offset += sprintf(json + offset, "\",\"Cardinality\":%d,", cardinality);
    offset += sprintf(json + offset, "\"Entropy\":%lf,", entropy);
    offset += sprintf(json + offset, "\"HeavyPart\":[");
    for (int i = 0; i < heavy_part.size(); ++i)
    {
        FIVE_TUPLE quintet = heavy_part[i].first;
        int f = heavy_part[i].second;
        offset += sprintf(json + offset, "[%u,%u,%d,%d,%d,%d]", quintet.srcIP, quintet.dstIP, quintet.srcPort, quintet.dstPort, quintet.protocal, f);
        if (i != (heavy_part.size() - 1))
            offset += sprintf(json + offset, ",");
    }
    offset += sprintf(json + offset, "],");
    offset += sprintf(json + offset, "\"Distribution\":[");
    for (int i = 0; i < dist.size(); ++i)
    {
        vector<int> tmp = dist[i];
        offset += sprintf(json + offset, "[");
        for (int j = 1; j < tmp.size(); ++j){
            offset += sprintf(json + offset, "(%d,%d)", j, tmp[j]);
            if (j != (tmp.size() - 1))
                offset += sprintf(json + offset, ",");
        }
        offset += sprintf(json + offset, "]");
        if (i != (dist.size() - 1))
            offset += sprintf(json + offset, ",");
    }
    offset += sprintf(json + offset, "]");
    offset += sprintf(json + offset, "}\r\n");
}

void sendMsg()
{
    thread_args *targs = (thread_args *)malloc(sizeof(thread_args));
    char *buf = (char *)malloc(MAX_LENGTH * sizeof(char));
    char *json = (char *)malloc(MAX_LENGTH * sizeof(char));

    targs->connectfd = socket(AF_INET, SOCK_STREAM, 0);
    targs->addr.sin_family = AF_INET;
    targs->addr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, SERV_IP, &targs->addr.sin_addr.s_addr);
    connect(targs->connectfd, (struct sockaddr *)&targs->addr, sizeof(targs->addr));
    printf("The connection to %s:%d has been established.\n", SERV_IP, SERV_PORT);

    char header[1023];
    int offset = 0;
    memset(header, 0, 1023);
    offset += sprintf(header + offset, "POST /post HTTP/1.1\r\n");
    offset += sprintf(header + offset, "Host: http://");
    offset += sprintf(header + offset, "%s:%d\r\n", SERV_IP, SERV_PORT);
    offset += sprintf(header + offset, "Content-Type: application/json\r\n");
    this_thread::sleep_for(chrono::seconds(9));
    char clean[100] = "GET /clean HTTP/1.1\r\nHost: http://10.128.189.187:5001\r\n\r\n";
    send(targs->connectfd, clean, strlen(clean), 0);
    this_thread::sleep_for(chrono::seconds(1));
    char msg[BUFSIZ];
    recv(targs->connectfd, msg, BUFSIZ, 0);
    printf("return message:\n %s\n", msg);
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(4));
        memset(json, 0, MAX_LENGTH * sizeof(char));
        memset(buf, 0, MAX_LENGTH * sizeof(char));
        int ofs = 0;
        ofs += sprintf(buf + ofs, "%s", header);
        makeJSON(json);
        int len = strlen(json);
        ofs += sprintf(buf + ofs, "Content-Length: ");
        ofs += sprintf(buf + ofs, "%d\r\n\r\n", len);
        //printf("%s\n", json);
        ofs += sprintf(buf + ofs, "%s\r\n\r\n", json);
        printf("sending:\n%s\n", buf);
        
        int s = send(targs->connectfd, buf, strlen(buf), 0);
        printf("%d bytes have been sent.\n", s);
        char rtmsg[BUFSIZ];
        this_thread::sleep_for(chrono::seconds(1));
        recv(targs->connectfd, rtmsg, BUFSIZ, 0);
        printf("return message: %s\n", rtmsg);
    }
    close(targs->connectfd);
    free(buf);
    free(targs);
}

void recieveSketch(thread_args &targs)
{
    ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *ptr = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
    char *buf = (char *)malloc(sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
    mtx.lock();
    sketches.push_back(ptr);
    SketchRepository::iterator it = sketches.end() - 1;
    mtx.unlock();
    while (true)
    {
        memset(buf, 0, sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
        int len = 0;
        char *buf_r = (char *)malloc(BUFSIZ);
        while(len < sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>)){
            memset(buf_r, 0, BUFSIZ);
            int r = recv(targs.connectfd, buf_r, BUFSIZ, 0);
            if (r == -1){
                printf("recv error.\n");
                return;
            }
            //printf("%d bytes have recieved.\n", r);
            len += r;
            strcat(buf, buf_r);
        }
        free(buf_r);
        //printf("Totally %d bytes have recieved.\n", len);
        printf("Sketch is set to be %d bytes.\n",  sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
        if (len == sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>))
        {
            mtx.lock();
            memcpy((*it), buf, sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
            mtx.unlock();
        }
    }
    close(targs.connectfd);
    free(buf);
    free(ptr);
    sketches.erase(it);
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
        thread_args targs;
        targs.connectfd = accept(listenfd, (struct sockaddr *)&targs.addr, &targs.addr_len);
        printf("A new connection has accepted!\n");
        fflush(stdout);
        thread getSketch(recieveSketch, ref(targs));
        getSketch.detach();
    }
    close(listenfd);
    monitor.join();
}

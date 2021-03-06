#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <regex.h>
#include <map>

#include "../elastic/ElasticSketch.h"
using namespace std;

#define QUEUE 10
#define MAX_LENGTH 500000
#define HEAVY_MEM (124 * 64)
#define BUCKET_NUM (HEAVY_MEM / 124)
#define TOT_MEM_IN_BYTES (16 * 1024)

#define THRESHOLD 1

int port = 32399;
int serv_port = 5001;
char serv_ip[20] = "127.0.0.1";
int seed_l = 323;
int seed_h = 1999;

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
typedef struct
{
    uint32_t val;
    bool swap_flag;
} PKT_DAT;

typedef vector<ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *> SketchRepository;
SketchRepository sketches;
map<int, int> client_id;
mutex mtx;

mutex mtx1;
mutex mtx2;
mutex mtx3;
mutex mtx4;

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
void makeJSON(char *json);

void sendMsg();

void recieveSketch(int connectfd);

void print_usage(char *program);

void read_args(int argc, char *argv[]);

void process_data(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic, int &cardinality, int &tot, double &entr, vector<vector<int>> &dist,
                  vector<pair<FIVE_TUPLE, PKT_DAT>> &heavy_part, int i);

int main(int argc, char *argv[])
{
    read_args(argc, argv);
    
    thread monitor(sendMsg);
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
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
        targs.connectfd = accept(listenfd, (struct sockaddr *)&(targs.addr), &(targs.addr_len));
        printf("A new connection has accepted!\n");
        fflush(stdout);
        thread getSketch(recieveSketch, targs.connectfd);
        getSketch.detach();
    }
    close(listenfd);
    monitor.join();
}

void read_args(int argc, char *argv[])
{
    int i = 1;
    bool error = false;

    if (argc == 1)
    {
        print_usage(argv[0]);
        exit(EXIT_SUCCESS);
    }

    while (i < argc)
    {
        if (strlen(argv[i]) == 2 && strcmp(argv[i], "-i") == 0)
        {
            if (i + 1 < argc)
            {
                memset(serv_ip, 0, sizeof(serv_ip));
                strncpy(serv_ip, argv[i + 1], strlen(argv[i + 1]));
                regex_t reg;
                int cflags;
                char ebuff[256];
                cflags = REG_EXTENDED;
                regmatch_t rm;
                int ret = 0;

                if ((ret = regcomp(&reg, "^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}$", cflags)) != 0){
                    regerror(ret, &reg, ebuff, 256);
                    printf("%s\n", ebuff);
                    regfree(&reg);
                    exit(EXIT_FAILURE);
                }
                if ((ret = regexec(&reg, serv_ip, 1, &rm, 0)) != 0){
                    regerror(ret, &reg, ebuff, 256);
                    printf("%s\n", ebuff);
                    regfree(&reg);
                    exit(EXIT_FAILURE);
                }
                if (rm.rm_so == -1){
                    printf("Invalid ip address: %s.\n", serv_ip);
                    print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
                regfree(&reg);
                i += 2;
            }
            else
            {
                printf("Cannot read server ip.\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-p") == 0)
        {
            if (i + 1 < argc)
            {
                port = (int)strtol(argv[i + 1], NULL, 10);
                if (port <= 0 || port > 65535) {
                    printf("Invalid port number: %d.\n", port);
                    exit(EXIT_FAILURE);
                }
                i += 2;
            }
            else
            {
                printf("Cannot read port number.\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-d") == 0)
        {
            if (i + 1 < argc)
            {
                serv_port = (int)strtoul(argv[i + 1], NULL, 10);
                if (serv_port <= 0 || serv_port > 65535)
                {
                    printf("Invalid port number: %d.\n", serv_port);
                    exit(EXIT_FAILURE);
                }
                i += 2;
            }
            else
            {
                printf("Cannot read port number.\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-h") == 0)
        {
            if (i + 1 < argc)
            {
                seed_h = (int)strtoul(argv[i + 1], NULL, 10);
                if (seed_h <= 0)
                {
                    printf("Invalid seed number: %d.\n", seed_h);
                    exit(EXIT_FAILURE);
                }
                i += 2;
            }
            else
            {
                printf("Cannot read seed number.\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else if (strlen(argv[i]) == 2 && strcmp(argv[i], "-l") == 0)
        {
            if (i + 1 < argc)
            {
                seed_l = (int)strtoul(argv[i + 1], NULL, 10);
                if (seed_l <= 0)
                {
                    printf("Invalid seed number: %d.\n", seed_l);
                    exit(EXIT_FAILURE);
                }
                i += 2;
            }
            else
            {
                printf("Cannot read seed number.\n");
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            printf("Invalid option %s\n", argv[i]);
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}

void print_usage(char *program)
{
    printf("Usage: %s [options]\n", program);
    printf("-i <server ip>   database server ip (default %s).\n", serv_ip);
    printf("-d <server port> database server port (default %d).\n", serv_port);
    printf("-p <port>        port number (default %d).\n", port);
    printf("-h <heavy seed>  seed to initiate BOBHash for heavy part (default %d)\n", seed_h);
    printf("-l <light seed>  seed to initiate BOBHash for light part (default %d)\n", seed_l);
    printf("Improtant: seed must be consist with elastic sketch.\n");
}

void sendMsg()
{
    thread_args *gtargs = (thread_args *)malloc(sizeof(thread_args));
    char *buf = (char *)malloc(MAX_LENGTH * sizeof(char));
    char *json = (char *)malloc(MAX_LENGTH * sizeof(char));

    gtargs->connectfd = socket(AF_INET, SOCK_STREAM, 0);
    gtargs->addr.sin_family = AF_INET;
    gtargs->addr.sin_port = htons(serv_port);
    inet_pton(AF_INET, serv_ip, &gtargs->addr.sin_addr.s_addr);
    int t = 0;
    while(connect(gtargs->connectfd, (struct sockaddr *)&gtargs->addr, sizeof(gtargs->addr)) == -1){
        ++t;
        printf("connect failed.\n");
        this_thread::sleep_for(chrono::seconds(5));
        if (t > 10)
        {
            printf("Timeout: Unable to connect to database %s:%d.\n", serv_ip, serv_port);
            exit(EXIT_FAILURE);
        }
    }
    printf("The connection to %s:%d has been established, then sending GET request.\n", serv_ip, serv_port);
    char header[1023];
    int offset = 0;
    memset(header, 0, 1023);
    offset += sprintf(header + offset, "POST /post HTTP/1.1\r\n");
    offset += sprintf(header + offset, "Host: http://");
    offset += sprintf(header + offset, "%s:%d\r\n", serv_ip, serv_port);
    offset += sprintf(header + offset, "Content-Type: application/json\r\n");

    this_thread::sleep_for(chrono::seconds(1));
    char clean[100] = "GET /clean HTTP/1.1\r\nHost: http://";
    strcat(clean, serv_ip);
    strcat(clean, ":");
    char tmpp[10];
    sprintf(tmpp, "%d\r\n\r\n", serv_port);
    strcat(clean, tmpp);
    send(gtargs->connectfd, clean, strlen(clean), 0);
    this_thread::sleep_for(chrono::seconds(1));
    char msg[BUFSIZ];
    recv(gtargs->connectfd, msg, BUFSIZ, 0);
    printf("return message:\n%s\n", msg);
    close(gtargs->connectfd);
    free(gtargs);

    int cnt = 0;
    while (true)
    {
        thread_args *ptargs = (thread_args *)malloc(sizeof(thread_args));
        ptargs->connectfd = socket(AF_INET, SOCK_STREAM, 0);
        ptargs->addr.sin_family = AF_INET;
        ptargs->addr.sin_port = htons(serv_port);
        inet_pton(AF_INET, serv_ip, &ptargs->addr.sin_addr.s_addr);
        int t = 0;
        while (connect(ptargs->connectfd, (struct sockaddr *)&ptargs->addr, sizeof(ptargs->addr)) == -1)
        {
            ++t;
            printf("connect failed.\n");
            this_thread::sleep_for(chrono::seconds(1));
            if (t > 5)
            {
                printf("Timeout: Unable to connect to database %s:%d.\n", serv_ip, serv_port);
                exit(EXIT_FAILURE);
            }
        }
        printf("The connection to %s:%d has been established, then sending POST request.\n", serv_ip, serv_port);
        ++cnt;
        this_thread::sleep_for(chrono::seconds(1));

        memset(json, 0, MAX_LENGTH * sizeof(char));
        memset(buf, 0, MAX_LENGTH * sizeof(char));
        int ofs = 0;
        ofs += sprintf(buf + ofs, "%s", header);
        makeJSON(json);
        int len = strlen(json);
        ofs += sprintf(buf + ofs, "Content-Length: ");
        ofs += sprintf(buf + ofs, "%d\r\n\r\n", len);
        ofs += sprintf(buf + ofs, "%s\r\n\r\n", json);
        //printf("sending:\n%s\n", buf);

        //printf("content:\n%s\n", buf);
        int s = send(ptargs->connectfd, buf, strlen(buf), 0);
        printf("%d bytes have been sent.\n", s);
        char rtmsg[BUFSIZ];
        this_thread::sleep_for(chrono::seconds(1));
        recv(ptargs->connectfd, rtmsg, BUFSIZ, 0);
        printf("return message:\n%s\n", rtmsg);
        close(ptargs->connectfd);
        free(ptargs);
    }
    
    free(buf);
}

void process_data(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *ptr, int &cardinality, int &tot, double &entr, vector<vector<int>> &dist,
                vector<pair<FIVE_TUPLE, PKT_DAT>> &heavy_part, int k)
{
    ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
    BOBHash32 *hash_fh = (elastic->heavy_part.bobhash);
    BOBHash32 *hash_fl = (elastic->light_part.bobhash);
    mtx.lock();
    memcpy(elastic, ptr, sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
    mtx.unlock();
    elastic->heavy_part.bobhash = hash_fh;
    elastic->light_part.bobhash = hash_fl;

    vector<int> tmp_dist;
    elastic->get_distribution(ref(tmp_dist));
    mtx1.lock();
    if (client_id[k] - 1 >= dist.size()){
        dist.resize(client_id[k]);
    }
    dist[client_id[k] - 1] = tmp_dist;
    mtx1.unlock();

    mtx2.lock();
    cardinality += elastic->get_cardinality();
    mtx2.unlock();

    for (int i = 1; i < 256; i++)
    {
        mtx3.lock();
        tot += elastic->light_part.mice_dist[i] * i;
        entr += elastic->light_part.mice_dist[i] * i * log2(i);
        mtx3.unlock();
    }
    for (int i = 0; i < BUCKET_NUM; ++i)
    {
        for (int j = 0; j < MAX_VALID_COUNTER; ++j)
        {
            char key[KEY_LENGTH_13];
            FIVE_TUPLE *quintet = new FIVE_TUPLE;
            PKT_DAT *packet = new PKT_DAT;
            packet->swap_flag = false;
            memcpy(key, elastic->heavy_part.buckets[i].key[j], KEY_LENGTH_13);
            memcpy(quintet, elastic->heavy_part.buckets[i].key[j], KEY_LENGTH_13);
            int val = elastic->heavy_part.buckets[i].val[j];
            int ex_val = elastic->light_part.query(key);
            if (HIGHEST_BIT_IS_1(val))
            {
                packet->swap_flag = true;
                if (ex_val)
                {
                    val += ex_val;
                    mtx3.lock();
                    tot -= ex_val;
                    entr -= ex_val * log2(ex_val);
                    mtx3.unlock();
                }
            }
            val = GetCounterVal(val);
            if (val)
            {
                packet->val = val;
                mtx3.lock();
                tot += val;
                entr += val * log2(val);
                mtx3.unlock();
                mtx4.lock();
                heavy_part.push_back(make_pair((*quintet), (*packet)));
                mtx4.unlock();
            }
            delete quintet;
            delete packet;
        }
    }
    delete elastic;
}

void writeHeavy(char *str, vector<pair<FIVE_TUPLE, PKT_DAT>> &heavy_part)
{
    int offset = 0;
    offset += sprintf(str + offset, "\"HeavyPart\":[");
    int hsize = heavy_part.size();
    for (int i = 0; i < hsize; ++i)
    {
        FIVE_TUPLE quintet = heavy_part[i].first;
        PKT_DAT packet = heavy_part[i].second;
        offset += sprintf(str + offset, "[%u,%u,%d,%d,%d,%d,%d]", quintet.srcIP, quintet.dstIP,
                          quintet.srcPort, quintet.dstPort, quintet.protocal, packet.val, packet.swap_flag);
        if (i < (hsize - 1))
            offset += sprintf(str + offset, ",");
    }
    offset += sprintf(str + offset, "],");
}

void writeDist(char *str, vector<vector<int>> &dist)
{
    int offset = 0;
    offset += sprintf(str + offset, "\"Distribution\":[");
    int dsize = dist.size();
    for (int i = 0; i < dsize; ++i)
    {
        vector<int> tmp = dist[i];
        offset += sprintf(str + offset, "[");
        int tsize = tmp.size();
        for (int j = 0; j < tsize; ++j)
        {
            if (tmp[j] > 0)
            {
                offset += sprintf(str + offset, "[%d,%d]", j, tmp[j]);
                if (j < (tsize - 1))
                    offset += sprintf(str + offset, ",");
            }
        }
        offset += sprintf(str + offset, "]");
        if (i < (dsize - 1))
            offset += sprintf(str + offset, ",");
    }
    offset += sprintf(str + offset, "]");
}


void makeJSON(char *json)
{
    ++id;
    int cardinality = 0;
    double entropy;
    vector<vector<int>> dist;
    vector<pair<FIVE_TUPLE, PKT_DAT>> heavy_part;
    int tot = 0;
    double entr = 0;

    time_t raw_time;
    struct tm timeinfo;
    time(&raw_time);
    timeinfo = *(localtime(&raw_time));
    vector<thread> threads;
    int i = 0;
    for (SketchRepository::iterator it = sketches.begin(); it != sketches.end(); ++it)
    {
        ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *ptr = (*it);
        ++i;
        if (ptr == NULL){
            continue;
        }
        threads.emplace_back(process_data, ptr, ref(cardinality), ref(tot), ref(entr), ref(dist), ref(heavy_part), i - 1);
    }

    for (int i = 0; i < threads.size(); ++i){
        threads[i].join();
    }
    threads.clear();
    if (tot > 0){
        entropy = -entr / (double)tot + log2(tot);
    }
    else{
        entropy = 0;
    }

    int offset = 0;
    offset += sprintf(json + offset, "{");
    offset += sprintf(json + offset, "\"ID\":%d,", id);
    offset += sprintf(json + offset, "\"Algorithm\":\"elastic\",");
    offset += sprintf(json + offset, "\"Time\":\"%s", asctime(&timeinfo));
    --offset;
    offset += sprintf(json + offset, "\",\"Cardinality\":%d,", cardinality);
    //printf("Cardinality: %d\n", cardinality);
    offset += sprintf(json + offset, "\"Entropy\":%lf,", entropy);

    char *buf_h = (char *)malloc(MAX_LENGTH);
    char *buf_d = (char *)malloc(MAX_LENGTH);
    thread worker_h(writeHeavy, buf_h, ref(heavy_part));
    thread worker_d(writeDist, buf_d, ref(dist));
    worker_h.join();
    worker_d.join();
    strcat(json, buf_h);
    strcat(json, buf_d);
    strcat(json, "}\r\n");
    delete buf_h;
    delete buf_d;
}

void recieveSketch(int connectfd)
{
    ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *ptr = (ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *)malloc(sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
    char *buf = (char *)malloc(sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
    char str[4];
    if (recv(connectfd, str, 4, 0) == -1)
    {
        printf("send error.\n");
        exit(EXIT_FAILURE);
    }
    int cid = atoi(str);
    mtx.lock();
    sketches.push_back(ptr);
    client_id[(int)(sketches.size() - 1)] = cid;
    mtx.unlock();

    while (true)
    {
        memset(buf, 0, sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
        int len = 0;
        char *buf_r = (char *)malloc(BUFSIZ);
        while (len < sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>))
        {
            memset(buf_r, 0, BUFSIZ);
            int r = recv(connectfd, buf_r, BUFSIZ, 0);
            if (r == -1)
            {
                printf("recv error.\n");
                exit(EXIT_FAILURE);
            }
            //printf("%d bytes have recieved.\n", r);
            memcpy((buf + len), buf_r, r);
            len += r;
        }
        free(buf_r);
        //printf("Totally %d bytes have recieved.\n", len);
        if (len == sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>))
        {
            mtx.lock();
            memcpy(ptr, buf, sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
            mtx.unlock();
        }
    }
    close(connectfd);
    free(buf);
    delete ptr;
}
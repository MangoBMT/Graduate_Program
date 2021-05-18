#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "../elastic/ElasticSketch.h"
using namespace std;

#define QUEUE 10
#define MAX_LENGTH 1048575
#define HEAVY_MEM (128 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 128)
#define TOT_MEM_IN_BYTES (512 * 1024)

int port = 32399;
int serv_port = 5001;
char serv_ip[20] = '127.0.0.1';
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
void makeJSON(char *json);

void sendMsg();

void recieveSketch(thread_args &targs);

void print_usage(char *program);

void read_args(int argc, char *argv[]);


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
        targs.connectfd = accept(listenfd, (struct sockaddr *)&targs.addr, &targs.addr_len);
        printf("A new connection has accepted!\n");
        fflush(stdout);
        thread getSketch(recieveSketch, ref(targs));
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

                if (regcomp(&reg, "^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}$", cflags) != 1){
                    regerror(ret, &reg, ebuff, 256);
                    printf("%s\n", ebuff);
                    freereg(&reg);
                    exit(EXIT_FAILURE);
                }
                if (regexec(&reg, serv_ip, 1, &rm, 0) != 1){
                    regerror(ret, &reg, ebuff, 256);
                    printf("%s\n", ebuff);
                    freereg(&reg);
                    exit(EXIT_FAILURE);
                }
                if (rm.rm_so == -1){
                    printf("Invalid ip address: %s.\n", serv_ip);
                    print_usage(argv[0]);
                    exit(EXIT_FAILURE);
                }
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
    thread_args *ptargs = (thread_args *)malloc(sizeof(thread_args));
    thread_args *gtargs = (thread_args *)malloc(sizeof(thread_args));
    char *buf = (char *)malloc(MAX_LENGTH * sizeof(char));
    char *json = (char *)malloc(MAX_LENGTH * sizeof(char));

    ptargs->connectfd = socket(AF_INET, SOCK_STREAM, 0);
    ptargs->addr.sin_family = AF_INET;
    ptargs->addr.sin_port = htons(serv_port);
    gtargs->connectfd = socket(AF_INET, SOCK_STREAM, 0);
    gtargs->addr.sin_family = AF_INET;
    gtargs->addr.sin_port = htons(serv_port);
    inet_pton(AF_INET, serv_ip, &ptargs->addr.sin_addr.s_addr);
    inet_pton(AF_INET, serv_ip, &gtargs->addr.sin_addr.s_addr);
    connect(ptargs->connectfd, (struct sockaddr *)&ptargs->addr, sizeof(ptargs->addr));
    connect(gtargs->connectfd, (struct sockaddr *)&gtargs->addr, sizeof(gtargs->addr));
    printf("The connection to %s:%d has been established.\n", serv_ip, serv_port);

    char header[1023];
    int offset = 0;
    memset(header, 0, 1023);
    offset += sprintf(header + offset, "POST /post HTTP/1.1\r\n");
    offset += sprintf(header + offset, "Host: http://");
    offset += sprintf(header + offset, "%s:%d\r\n", serv_ip, serv_port);
    offset += sprintf(header + offset, "Content-Type: application/json\r\n");
    this_thread::sleep_for(chrono::seconds(9));
    char clean[100] = "GET /clean HTTP/1.1\r\nHost: ";
    strcat(clean, serv_ip);
    strcat(clean, ":");
    char tmpp[10];
    sprintf(tmpp, "%d\r\n", serv_port);
    strcat(clean, tmpp);
    send(gtargs->connectfd, clean, strlen(clean), 0);
    this_thread::sleep_for(chrono::seconds(1));
    char msg[BUFSIZ];
    recv(gtargs->connectfd, msg, BUFSIZ, 0);
    printf("return message:\n%s\n", msg);
    close(gtargs->connectfd);
    free(gtargs);

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
        //printf("sending:\n%s\n", buf);

        int s = send(ptargs->connectfd, buf, strlen(buf), 0);
        printf("%d bytes have been sent.\n", s);
        char rtmsg[BUFSIZ];
        this_thread::sleep_for(chrono::seconds(1));
        recv(ptargs->connectfd, rtmsg, BUFSIZ, 0);
        printf("return message: %s\n", rtmsg);
    }
    close(ptargs->connectfd);
    free(buf);
    free(ptargs);
}

void makeJSON(char *json)
{
    ++id;
    int cardinality = 0;
    double entropy;
    vector<vector<int>> dist;
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
        if ((*it) == NULL)
        {
            free(elastic);
            continue;
        }
        memcpy(elastic, (*it), sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
        mtx.unlock();
        vector<int> tmp_dist;
        elastic->get_distribution(tmp_dist);
        dist.push_back(tmp_dist);

        vector<pair<string, int>> tmp_heavy_part;
        elastic->get_heavy_hitters(1, tmp_heavy_part);
        for (int i = 0; i < tmp_heavy_part.size(); ++i)
        {
            FIVE_TUPLE *quintet = (FIVE_TUPLE *)calloc(KEY_LENGTH_13, sizeof(char));
            memcpy(quintet, &(tmp_heavy_part[i].first), KEY_LENGTH_13 * sizeof(char));
            heavy_part.push_back(make_pair((*quintet), tmp_heavy_part[i].second));
            free(quintet);
        }

        cardinality += elastic->get_cardinality();

        for (int i = 1; i < 256; i++)
        {
            tot += elastic->light_part.mice_dist[i] * i;
            entr += elastic->light_part.mice_dist[i] * i * log2(i);
        }
        for (int i = 0; i < BUCKET_NUM; ++i)
        {
            for (int j = 0; j < MAX_VALID_COUNTER; ++j)
            {
                char key[KEY_LENGTH_13];
                strncpy(key, elastic->heavy_part.buckets[i].key[j], KEY_LENGTH_13);
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
    if (tot >= 0)
        entropy = -entr / tot + log2(tot);
    else
        entropy = 0;

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
        for (int j = 1; j < tmp.size(); ++j)
        {
            offset += sprintf(json + offset, "[%d,%d]", j, tmp[j]);
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

void recieveSketch(thread_args &targs)
{
    ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *ptr = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
    delete ptr->heavy_part.bobhash;
    delete ptr->light_part.bobhash;
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
        while (len < sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>))
        {
            memset(buf_r, 0, BUFSIZ);
            int r = recv(targs.connectfd, buf_r, BUFSIZ, 0);
            if (r == -1)
            {
                printf("recv error.\n");
                return;
            }
            //printf("%d bytes have recieved.\n", r);
            len += r;
            strcat(buf, buf_r);
        }
        free(buf_r);
        //printf("Totally %d bytes have recieved.\n", len);
        if (len == sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>))
        {
            mtx.lock();
            memcpy((*it), buf, sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
            (*it)->heavy_part.bobhash = new BOBHash32(seed_h % MAX_PRIME32);
            (*it)->light_part.bobhash = new BOBHash32(seed_l % MAX_PRIME32);
            mtx.unlock();
        }
        fflush(stdout);
    }
    close(targs.connectfd);
    free(buf);
    free(ptr);
    sketches.erase(it);
}
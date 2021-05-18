#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <ifaddrs.h>
#include <regex.h>

#include "../elastic/ElasticSketch.h"
using namespace std;

#define HEAVY_MEM (128 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 128)
#define TOT_MEM_IN_BYTES (512 * 1024)

char serv_ip[20] =  "192.168.1.10";
int serv_port = 32399;
char ip[20] = "0.0.0.0";
struct FIVE_TUPLE
{
    uint32_t srcIP;
    uint32_t dstIP;
    uint16_t srcPort;
    uint16_t dstPort;
    uint8_t protocal;
};

ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
typedef vector<FIVE_TUPLE> TRACE;
TRACE traces;
mutex mtx;
atomic_int pnum(0);

void packetHandler(u_char *userData, const struct pcap_pkthdr *pkthdr, const u_char *packet);

void packetCapture();

void deliverSketch();

void print_usage(char *program);

void read_args(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    read_args(argc, argv);

    struct ifaddrs *ifAddrStruct = NULL;
    getifaddrs(&ifAddrStruct);
    while (ifAddrStruct != NULL)
    {
        if (ifAddrStruct->ifa_addr->sa_family == AF_INET && strcmp(ifAddrStruct->ifa_name, "eth0") == 0)
        {
            void *tmpAddrPtr;
            tmpAddrPtr = &((struct sockaddr_in *)ifAddrStruct->ifa_addr)->sin_addr;
            char addressBuffer[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            strcpy(ip, addressBuffer);
            break;
        }
        ifAddrStruct = ifAddrStruct->ifa_next;
    }
    if (strcmp(ip, "0.0.0.0") == 0){
        printf("ip address error.\n");
        return 0;
    }
    thread pcap(packetCapture);
    thread monitor(deliverSketch);
    pcap.join();
    monitor.join();
    return 0;
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

                if ((ret = regcomp(&reg, "^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}$", cflags)) != 0)
                {
                    regerror(ret, &reg, ebuff, 256);
                    printf("%s\n", ebuff);
                    regfree(&reg);
                    exit(EXIT_FAILURE);
                }
                if ((ret = regexec(&reg, serv_ip, 1, &rm, 0)) != 0)
                {
                    regerror(ret, &reg, ebuff, 256);
                    printf("%s\n", ebuff);
                    regfree(&reg);
                    exit(EXIT_FAILURE);
                }
                if (rm.rm_so == -1)
                {
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
                serv_port = (int)strtol(argv[i + 1], NULL, 10);
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
    printf("-i <server ip>   server ip (default %s).\n", serv_ip);
    printf("-p <server port> server port (default %d).\n", serv_port);
}

void packetCapture()
{
    int f;
    pcap_t *descr;
    pcap_if_t *alldevs;
    char errbuf[PCAP_ERRBUF_SIZE];
    f = pcap_findalldevs(&alldevs, errbuf);
    if (f == -1)
    {
        cout << "pcap_lookupdev() failed: " << errbuf << endl;
        return;
    }
    descr = pcap_open_live(alldevs->name, BUFSIZ, 0, -1, errbuf);
    if (descr == NULL)
    {
        cout << "pcap_open_live() failed: " << errbuf << endl;
        return;
    }
    pnum = 0;
    if (pcap_loop(descr, -1, packetHandler, NULL) < 0)
    {
        cout << "pcap_loop() failed: " << pcap_geterr(descr);
        return;
    }
}

void deliverSketch()
{
    int cfd;
    struct sockaddr_in serv_addr;
    char *buf = NULL;
    buf = (char *)malloc(sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));

    cfd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(serv_port);
    inet_pton(AF_INET, serv_ip, &serv_addr.sin_addr.s_addr);

    if (connect(cfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
    {
        printf("connect failed.\n");
        return;
    }
    int cnt = 0;
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(5));
        mtx.lock();
        memcpy(buf, elastic, sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>));
        mtx.unlock();
        if (send(cfd, buf, sizeof(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>), 0) == -1)
        {
            printf("send error.\n");
            return;
        }
        ++cnt;
        if (!(cnt % 100))
        {
            printf("updated sketch to server for %d times.\n", cnt);
        }
    }
    close(cfd);
    free(buf);
}

void packetHandler(u_char *userData, const struct pcap_pkthdr *pkthdr, const u_char *packet)
{
    //printf("1 packet captured.\n");
    struct FIVE_TUPLE *quintet = new struct FIVE_TUPLE;
    char *key = new char[KEY_LENGTH_13];
    char *content = new char[BUFSIZ];
    //unsigned int len = pkthdr->len;
    unsigned int len = 1;
    memcpy(content, (const char *)packet, (int)pkthdr->caplen);
    quintet->srcIP = *(uint32_t *)(content + 26);
    quintet->dstIP = *(uint32_t *)(content + 30);
    quintet->srcPort = *(uint16_t *)(content + 34);
    quintet->dstPort = *(uint16_t *)(content + 36);
    quintet->protocal = *(uint8_t *)(content + 23);
    /*
    printf("scrIP:%u.%u.%u.%u\ndstIP:%u.%u.%u.%u\nsrcPort:%d\ndstport:%d\nprotocal:%d\n\n", (uint8_t)content[26], (uint8_t)content[27], (uint8_t)content[28],
           (uint8_t)content[29], (uint8_t)content[30], (uint8_t)content[31], (uint8_t)content[32], (uint8_t)content[33],
           quintet->srcPort, quintet->dstPort, quintet->protocal);
    */
    char sip[20];
    char dip[20];
    sprintf(sip, "%u.%u.%u.%u", (uint8_t)content[26], (uint8_t)content[27], (uint8_t)content[28], (uint8_t)content[29]);
    sprintf(dip, "%u.%u.%u.%u", (uint8_t)content[30], (uint8_t)content[31], (uint8_t)content[32], (uint8_t)content[33]);
    if ((quintet->protocal == 6 || quintet->protocal == 17) && strcmp(dip, ip) == 0 && strcmp(sip, serv_ip) != 0)
    {
        memcpy(key, quintet, sizeof(quintet));
        mtx.lock();
        elastic->insert(key, len);
        mtx.unlock();
        ++pnum;
        cout << pnum << " packets have been captured.\n";
    }
    delete quintet;
    delete[] key;
    delete[] content;
}
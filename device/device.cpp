#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../elastic/ElasticSketch.h"
using namespace std;

#define HEAVY_MEM (128 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 128)
#define TOT_MEM_IN_BYTES (512 * 1024)

#define SERV_IP "172.18.0.1"
#define SERV_PORT 32399
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

void packetHandler(u_char *userData, const struct pcap_pkthdr *pkthdr, const u_char *packet)
{
    //printf("1 packet captured.\n");
    struct FIVE_TUPLE *quintet = new struct FIVE_TUPLE;
    char *key = new char[13];
    char *content = new char[65535];
    //unsigned int len = pkthdr->len;
    unsigned int len = 1;
    memcpy(content, (const char *)packet, (int)pkthdr->caplen);
    quintet->srcIP = *(uint32_t *)(content + 26);
    quintet->dstIP = *(uint32_t *)(content + 30);
    quintet->srcPort = *(uint16_t *)(content + 34);
    quintet->dstPort = *(uint16_t *)(content + 36);
    quintet->protocal = *(uint8_t *)(content + 23);
    memcpy(key, quintet, sizeof(quintet));
    mtx.lock();
    elastic->insert(key, len);
    mtx.unlock();
    delete quintet;
    delete[] key;
    delete[] content;
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
    buf = (char *)malloc(TOT_MEM_IN_BYTES * sizeof(char));

    cfd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, SERV_IP, &serv_addr.sin_addr.s_addr);

    connect(cfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    while (true)
    {
        this_thread::sleep_for(chrono::seconds(1));
        mtx.lock();
        memcpy(buf, elastic, TOT_MEM_IN_BYTES);
        mtx.unlock();
        send(cfd, buf, TOT_MEM_IN_BYTES, 0);
        printf("sketch sent.\n");
    }
    close(cfd);
    free(buf);
}

int main()
{
    thread pcap(packetCapture);
    thread monitor(deliverSketch);
    pcap.join();
    monitor.join();
}

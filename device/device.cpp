#include "../elastic/ElasticSketch.h"
using namespace std;

#define HEAVY_MEM (136 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 136)
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
    struct FIVE_TUPLE *quintet = new struct FIVE_TUPLE;
    uint8_t *key = new uint8_t[13];
    unsigned char *content = new unsigned char[65535];
    //unsigned int len = pkthdr->len;
    unsigned int len = 1;
    memcpy(content, (const unsigned char *)packet, (int)pkthdr->caplen);
    quintet->srcIP = (uint32_t)(content + 26);
    quintet->dstIP = (uint32_t)(content + 30);
    quintet->srcPort = (uint16_t)(content + 34);
    quintet->dstPort = (uint16_t)(content + 36);
    quintet->protocal = (uint8_t)(content + 23);
    memcpy(key, quintet, sizeof(quintet));
    mtx.lock();
    elastic->insert(key, len);
    mtx.unlock();
    delete quintet;
    delete key;
    delete content;
}

void packetCapture()
{
    char *dev;
    pcap_t *descr;
    char errbuf[PCAP_ERRBUF_SIZE];

    dev = pcap_lookupdev(errbuf);
    if (dev == NULL)
    {
        cout << "pcap_lookupdev() failed: " << errbuf << endl;
        return;
    }
    descr = pcap_open_live(dev, BUFSIZ, 0, -1, errbuf);
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
    buf = (char*)malloc(TOT_MEM_IN_BYTES *  sizeof(char));

    cfd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERV_PORT);
    inet_pton(AF_INET, SERV_IP, &serv_addr.sin_addr.s_addr);

    connect(cfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    while(true){
        this_thread::sleep_for(chrono::seconds(1));
        mtx.lock();
        memcpy(buf, elastic, TOT_MEM_IN_BYTES);
        mtx.unlock();
        sned(cfd, buf, TOT_MEM_IN_BYTES, 0);
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
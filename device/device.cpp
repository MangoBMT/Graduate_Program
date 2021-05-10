#include "../elastic/ElasticSketch.h"
using namespace std;

#define HEAVY_MEM (128 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 64)
#define TOT_MEM_IN_BYTES (512 * 1024)

struct FIVE_TUPLE
{
    uint32_t srcIP;
    uint32_t dstIP;
    uint16_t srcPort;
    uint16_t dstPort;
    uint8_t protocal;
};

typedef vector<FIVE_TUPLE> TRACE;
TRACE traces;
mutex mtx;

void update_sketch(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic, FIVE_TUPLE &tmp_five_tuple)
{
    int f = rand() % 3 + 1;
    mtx.lock();
    elastic->insert((uint8_t *)(tmp_five_tuple.key), f);
    mtx.unlock();
}

void send_sketch(ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic)
{
    ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *cur_elastic = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
    mtx.lock();
    memcpy(cur_elastic, elastic, TOT_MEM_IN_BYTES);
    mtx.unlock();
    // deliver sketch constantly.
    for (int j = 0; j < 100; ++j)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        vector<pair<string, int>> heavy_hitters;
        cur_elastic->get_heavy_hitters(2000, heavy_hitters);
        vector<double> dist;
        cur_elastic->get_distribution(dist);
        printf("est_cardinality=%d\t", cur_elastic->get_cardinality());
        printf("entropy=%.3lf\n", cur_elastic->get_entropy());
        /*printf("flow size distribution: <flow size, count>\n");
        for (int i = 0, j = 0; i < (int)dist.size(); ++i)
            if (dist[i] > 0)
            {
                printf("<%d, %d>", i, (int)dist[i]);
                if (++j % 10 == 0)
                    printf("\n");
                else
                    printf("\t");
            }
        printf("\n");
        printf("heavy hitters: <srcIP, count>, threshold=%d\n", 2000);
        for (int i = 0, j = 0; i < (int)heavy_hitters.size(); ++i)
        {
            uint32_t srcIP;
            memcpy(&srcIP, heavy_hitters[i].first.c_str(), 4);
            printf("<%.8x, %d>", srcIP, heavy_hitters[i].second);
            if (++j % 5 == 0)
                printf("\n");
            else
                printf("\t");
        }
        printf("\n");*/
    }
    delete cur_elastic;
}

void packetHandler(u_char *userData, const struct pcap_pkthdr *pkthdr, const u_char *packet)
{
    cout << ++packetCount << " packet(s) captured" << endl;
}

int main()
{
    char *dev;
    pcap_t *descr;
    char errbuf[PCAP_ERRBUF_SIZE];

    dev = pcap_lookupdev(errbuf);
    if (dev == NULL)
    {
        cout << "pcap_lookupdev() failed: " << errbuf << endl;
        return 1;
    }

    descr = pcap_open_live(dev, BUFSIZ, 0, -1, errbuf);
    if (descr == NULL)
    {
        cout << "pcap_open_live() failed: " << errbuf << endl;
        return 1;
    }

    if (pcap_loop(descr, 10, packetHandler, NULL) < 0)
    {
        cout << "pcap_loop() failed: " << pcap_geterr(descr);
        return 1;
    }

    cout << "capture finished" << endl;

    return 0;
}
#include "../elastic/ElasticSketch.h"
using namespace std;

#define HEAVY_MEM (128 * 1024)
#define BUCKET_NUM (HEAVY_MEM / 64)
#define TOT_MEM_IN_BYTES (512 * 1024)

struct FIVE_TUPLE
{
    char key[13];
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

int main()
{
    pid_t pid[11];
    for (int k = 0; k <= 10; ++k){
        pid[k] = fork();
        if (pid[k] < 0){
            printf("Create child process failed!\n");
        }
        else if (pid[k] == 0){
            char fileName[20];
            sprintf(fileName, "../data/%d.dat", k);
            FILE *fp = fopen(fileName, "rb");
            FIVE_TUPLE tmp_five_tuple;
            uint8_t tmp_f;
            while (fread(&tmp_five_tuple, 1, 13, fp) == 13)
            {
                traces.push_back(tmp_five_tuple);
            }
            fclose(fp);
            ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES> *elastic = new ElasticSketch<BUCKET_NUM, TOT_MEM_IN_BYTES>();
            printf("Successfully read in %s, %ld packets\n", fileName, traces.size());
            thread monitor(send_sketch, elastic);
            for (int i = 0; i < traces.size(); ++i)
            {
                thread t(update_sketch, elastic, ref(traces[i]));
                t.detach();
            }
            monitor.join();
            break;
        }
    }
    waitpid(0, NULL, 0);
}
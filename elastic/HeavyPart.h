#ifndef _HEAVYPART_H_
#define _HEAVYPART_H_

#include "Param.h"

template <int bucket_num>
class HeavyPart
{
public:
    alignas(64) Bucket buckets[bucket_num];
    BOBHash32 *bobhash = NULL;
    HeavyPart()
    {
        clear();
        uint32_t index = 1999;
        bobhash = new BOBHash32(index % MAX_PRIME32);
    }
    ~HeavyPart() {
        delete bobhash;
    }

    void clear()
    {
        memset(buckets, 0, sizeof(Bucket) * bucket_num);
    }

    /* insertion */
    int insert(unsigned char *key, unsigned char *swap_key, uint32_t &swap_val, uint32_t f = 1)
    {
        int pos = (uint32_t)bobhash->run((const char *)key, KEY_LENGTH_13) % (uint32_t)bucket_num;
        char zero_str = new char[KEY_LENGTH_13];
        memset(zero_str, 0, sizeof(char) * KEY_LENGTH_13)
        /* find if there has matched bucket */
        int matched = -1, empty = -1, min_counter = 0;
        uint32_t min_counter_val = GetCounterVal(buckets[pos].val[0]);
        for (int i = 0; i < COUNTER_PER_BUCKET - 1; i++)
        {
            if (strncmp(buckets[pos].key[i], key, KEY_LENGTH_13) == 0)
            {
                matched = i;
                break;
            }
            if (strncmp(buckets[pos].key[i], zero_str, KEY_LENGTH_13) == 0 && empty == -1)
                empty = i;
            if (min_counter_val > GetCounterVal(buckets[pos].val[i]))
            {
                min_counter = i;
                min_counter_val = GetCounterVal(buckets[pos].val[i]);
            }
        }
        delete []zero_str; 
        /* if matched */
        if (matched != -1)
        {
            buckets[pos].val[matched] += f;
            return 0;
        }

        /* if there has empty bucket */
        if (empty != -1)
        {
            strncpy(buckets[pos].key[empty], key, KEY_LENGTH_13);
            buckets[pos].val[empty] = f;
            return 0;
        }

        /* update guard val and comparison */
        uint32_t guard_val = buckets[pos].val[MAX_VALID_COUNTER];
        guard_val = UPDATE_GUARD_VAL(guard_val);

        if (!JUDGE_IF_SWAP(GetCounterVal(min_counter_val), guard_val))
        {
            buckets[pos].val[MAX_VALID_COUNTER] = guard_val;
            return 2;
        }

        strncpy(swap_key, buckets[pos].key[min_counter], KEY_LENGTH_13);
        swap_val = buckets[pos].val[min_counter];

        buckets[pos].val[MAX_VALID_COUNTER] = 0;

        strncpy(buckets[pos].key[min_counter], key, KEY_LENGTH_13);
        buckets[pos].val[min_counter] = 0x80000001;

        return 1;
    }

    /* query */
    uint32_t query(unsigned char *key)
    {
        int pos = (uint32_t)bobhash->run((const char *)key, KEY_LENGTH_13) % (uint32_t)bucket_num;

        for (int i = 0; i < MAX_VALID_COUNTER; ++i)
            if (strncmp(buckets[pos].key[i], key, KEY_LENGTH_13) == 0)
                return buckets[pos].val[i];

        return 0;
    }

    /* interface */
    int get_memory_usage()
    {
        return bucket_num * sizeof(Bucket);
    }
    int get_bucket_num()
    {
        return bucket_num;
    }
};

#endif

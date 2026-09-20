#include "hash64.h"


static uint32_t load_u32_le(const uint8_t *data) {
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}


uint64_t murmur_hash64(const void * key, int len, unsigned int seed) {
    const uint32_t m = 0x5bd1e995;
    const int r = 24;
    uint32_t h1 = seed ^ len;
    uint32_t h2 = 0;
    const uint8_t *data = (const uint8_t *)key;
    while(len >= 8) {
        uint32_t k1 = load_u32_le(data);
        data += 4;
        k1 *= m; k1 ^= k1 >> r; k1 *= m;
        h1 *= m; h1 ^= k1;
        len -= 4;
        uint32_t k2 = load_u32_le(data);
        data += 4;
        k2 *= m; k2 ^= k2 >> r; k2 *= m;
        h2 *= m; h2 ^= k2;
        len -= 4;
    }
    if(len >= 4) {
        uint32_t k1 = load_u32_le(data);
        data += 4;
        k1 *= m; k1 ^= k1 >> r; k1 *= m;
        h1 *= m; h1 ^= k1;
        len -= 4;
    }
    switch(len) {
        case 3: h2 ^= (uint32_t)data[2] << 16;
        case 2: h2 ^= (uint32_t)data[1] << 8;
        case 1: h2 ^= (uint32_t)data[0];
        h2 *= m;
    };
    h1 ^= h2 >> 18; h1 *= m;
    h2 ^= h1 >> 22; h2 *= m;
    h1 ^= h2 >> 17; h1 *= m;
    h2 ^= h1 >> 19; h2 *= m;
    uint64_t h = h1;
    h = (h << 32) | h2;
    return h;
}



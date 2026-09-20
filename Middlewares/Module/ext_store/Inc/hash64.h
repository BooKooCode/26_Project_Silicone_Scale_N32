#ifndef __HASH64_H__
#define __HASH64_H__

#include "stdint.h"


uint64_t murmur_hash64(const void * key, int len, unsigned int seed);

#define HASH_PRIME_SEED     0xEE6B27EB

#define HASH_64(key, len)   murmur_hash64(key, len, HASH_PRIME_SEED)

#endif


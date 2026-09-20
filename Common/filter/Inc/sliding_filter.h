#ifndef __SLIDING_FILTER_H__
#define __SLIDING_FILTER_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float *buf;
    uint32_t size;
    uint32_t idx;
    uint32_t count;

    float mean;
    float std;
    
    float sum;
    float sum_sq;

    float max;
    float min;
}sliding_stat_t;


void sliding_stat_init(sliding_stat_t *slide, float *buffer, uint32_t win_size);
void sliding_stat_reset(sliding_stat_t *slide);
void sliding_stat_push(sliding_stat_t *slide, float sample);
void sliding_stat_get(sliding_stat_t *slide);
bool sliding_stat_ready(sliding_stat_t *slide);

#ifdef __cplusplus
}
#endif

#endif

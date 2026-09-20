#include "sliding_filter.h"
#include <string.h>
#include <math.h>




void sliding_stat_init(sliding_stat_t *slide, float *buffer, uint32_t win_size)
{
    if (NULL == slide || NULL == buffer || 0 == win_size ) {
        return;
    }
    slide->buf   = buffer;
    slide->size  = win_size;
    sliding_stat_reset(slide);
}


void sliding_stat_reset(sliding_stat_t *slide)
{
    if (NULL == slide) {
        return;
    }
    slide->idx   = 0;
    slide->count = 0;
    slide->sum   = 0.0f;
    slide->sum_sq = 0.0f;
    slide->max   = 0.0f;
    slide->min   = 0.0f;
    if (slide->buf) {
        memset(slide->buf, 0, sizeof(float) * slide->size);
    }
}


void sliding_stat_push(sliding_stat_t *slide, float sample)
{
    if (NULL == slide || NULL == slide->buf) {
        return;
    }
    if (slide->count < slide->size) {
        slide->buf[slide->idx] = sample;
        slide->sum += sample;
        slide->sum_sq += sample * sample;

        if (slide->count == 0) {
            slide->max = sample;
            slide->min = sample;
        } else {
            if (sample > slide->max) slide->max = sample;
            if (sample < slide->min) slide->min = sample;
        }

        slide->count++;
    } 
    else {
        float old = slide->buf[slide->idx];
        slide->buf[slide->idx] = sample;
        slide->sum += sample - old;
        slide->sum_sq += sample * sample - old * old;

        if (old == slide->max || old == slide->min) {
            float max = slide->buf[0];
            float min = slide->buf[0];
            for (uint32_t i = 1; i < slide->size; i++) {
                if (slide->buf[i] > max) max = slide->buf[i];
                if (slide->buf[i] < min) min = slide->buf[i];
            }
            slide->max = max;
            slide->min = min;
        } else {
            if (sample > slide->max) slide->max = sample;
            if (sample < slide->min) slide->min = sample;
        }
    }
    slide->idx++;
    if (slide->idx >= slide->size) {
        slide->idx = 0;
    }
}


bool sliding_stat_ready(sliding_stat_t *slide)
{
    if (NULL == slide) {
        return false;
    }
    return (slide->count >= slide->size);
}


void sliding_stat_get(sliding_stat_t *slide)
{
    if (NULL == slide  || 0 == slide->count) {
        return ;
    }
    float mean = slide->sum / (float)slide->count;
    float var  = slide->sum_sq / (float)slide->count - mean * mean;
    if (var < 0.0f) {
        var = 0.0f;
    }
    slide->mean = mean;
    slide->std  = sqrtf(var);

}


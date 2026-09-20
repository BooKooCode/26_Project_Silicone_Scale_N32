#ifndef SCALE_PEELING_TIMEOUT_H__
#define SCALE_PEELING_TIMEOUT_H__

#include <stdbool.h>
#include <stdint.h>

#define SCALE_PEELING_SAMPLE_COUNT    20U
#define SCALE_PEELING_TIMEOUT_CYCLES  60U

typedef enum {
    SCALE_PEELING_ESTIMATE_LATEST = 0U,
    SCALE_PEELING_ESTIMATE_TRIMMED_MEAN,
    SCALE_PEELING_ESTIMATE_TREND_ENDPOINT,
} scale_peeling_estimate_mode_e;

typedef struct {
    float value;
    float trend_delta;
    float residual;
    scale_peeling_estimate_mode_e mode;
} scale_peeling_estimate_t;

typedef struct {
    float samples[SCALE_PEELING_SAMPLE_COUNT];
    float original_tare;
    uint32_t write_index;
    uint32_t sample_count;
    uint32_t elapsed_cycles;
    bool active;
} scale_peeling_session_t;

bool scale_peeling_estimate(const float *samples,
                            uint32_t count,
                            scale_peeling_estimate_t *result);

void scale_peeling_session_init(scale_peeling_session_t *session);
bool scale_peeling_session_start(scale_peeling_session_t *session, float original_tare);
void scale_peeling_session_push(scale_peeling_session_t *session, float sample);
bool scale_peeling_session_timed_out(const scale_peeling_session_t *session);
bool scale_peeling_session_estimate(const scale_peeling_session_t *session,
                                    scale_peeling_estimate_t *result);
float scale_peeling_session_cancel(scale_peeling_session_t *session);
void scale_peeling_session_complete(scale_peeling_session_t *session);

#endif

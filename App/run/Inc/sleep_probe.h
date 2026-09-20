#ifndef __SLEEP_PROBE_H__
#define __SLEEP_PROBE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#ifndef SLEEP_PROBE_CUP_WAKE_ENABLED
#define SLEEP_PROBE_CUP_WAKE_ENABLED            1U
#endif
#ifndef SLEEP_PROBE_PERIOD_MS
#define SLEEP_PROBE_PERIOD_MS                   1000UL
#endif
#ifndef SLEEP_PROBE_WINDOW_MS
#define SLEEP_PROBE_WINDOW_MS                   500UL
#endif
#ifndef SLEEP_PROBE_SAMPLE_POLL_MS
#define SLEEP_PROBE_SAMPLE_POLL_MS              5UL
#endif
#ifndef SLEEP_PROBE_WAKE_DELTA_G
#define SLEEP_PROBE_WAKE_DELTA_G                50.0f
#endif
#ifndef SLEEP_PROBE_OVERLOAD_LIMIT_G
#define SLEEP_PROBE_OVERLOAD_LIMIT_G            2000.0f
#endif
#ifndef SLEEP_PROBE_FAILURE_LIMIT
#define SLEEP_PROBE_FAILURE_LIMIT               3U
#endif
#ifndef SLEEP_PROBE_STABLE_SAMPLE_COUNT
#define SLEEP_PROBE_STABLE_SAMPLE_COUNT         16U
#endif

#if (SLEEP_PROBE_CUP_WAKE_ENABLED != 0U) && \
    (SLEEP_PROBE_CUP_WAKE_ENABLED != 1U)
#error "SLEEP_PROBE_CUP_WAKE_ENABLED must be 0 or 1"
#endif
#if SLEEP_PROBE_WINDOW_MS > SLEEP_PROBE_PERIOD_MS
#error "SLEEP_PROBE_WINDOW_MS must not exceed SLEEP_PROBE_PERIOD_MS"
#endif
#if (SLEEP_PROBE_SAMPLE_POLL_MS == 0UL) || \
    ((SLEEP_PROBE_SAMPLE_POLL_MS * SLEEP_PROBE_STABLE_SAMPLE_COUNT) > SLEEP_PROBE_WINDOW_MS)
#error "Sleep probe window cannot consume enough stable samples"
#endif

typedef enum {
    SLEEP_PROBE_EVENT_NONE = 0,
    SLEEP_PROBE_EVENT_CUP_WAKE,
    SLEEP_PROBE_EVENT_ERROR_WAKE,
    SLEEP_PROBE_EVENT_RECOVERED,
} sleep_probe_event_e;

typedef struct {
    float baseline_g;
    uint8_t failure_count;
    bool baseline_valid;
    bool error_latched;
} sleep_probe_t;

void sleep_probe_init(sleep_probe_t *probe, bool baseline_valid, float baseline_g);

sleep_probe_event_e sleep_probe_finish_window(sleep_probe_t *probe,
                                              bool sample_seen,
                                              bool driver_error,
                                              bool stable,
                                              float absolute_mass_g);

bool sleep_probe_has_baseline(const sleep_probe_t *probe);

float sleep_probe_baseline_g(const sleep_probe_t *probe);

uint8_t sleep_probe_failure_count(const sleep_probe_t *probe);

bool sleep_probe_error_latched(const sleep_probe_t *probe);

#ifdef __cplusplus
}
#endif

#endif
#ifndef __SLEEP_PROBE_H__
#define __SLEEP_PROBE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#ifndef SLEEP_PROBE_PERIOD_MS
#define SLEEP_PROBE_PERIOD_MS                    1000UL
#endif
/* One CS1237 frame at 1280 Hz takes 0.78 ms.  The trigger ends as soon as the
 * first frame arrives; this timeout only bounds the wait for that frame, so
 * keep enough margin for the 1 ms tick and SPI/DMA completion. */
#ifndef SLEEP_PROBE_FIRST_SAMPLE_TIMEOUT_MS
#define SLEEP_PROBE_FIRST_SAMPLE_TIMEOUT_MS      5UL
#endif
#ifndef SLEEP_PROBE_TRIGGER_SAMPLE_COUNT
#define SLEEP_PROBE_TRIGGER_SAMPLE_COUNT         1U
#endif
#ifndef SLEEP_PROBE_SAMPLE_POLL_MS
#define SLEEP_PROBE_SAMPLE_POLL_MS               1UL
#endif
#ifndef SLEEP_PROBE_STATE_POLL_MS
#define SLEEP_PROBE_STATE_POLL_MS                1UL
#endif
/* When a window first exceeds the +50 g delta, collect this many extra frames
 * and compare their average with the reference before waking, so a single
 * noisy conversion cannot raise a cup wake. */
#ifndef SLEEP_PROBE_CONFIRM_SAMPLE_COUNT
#define SLEEP_PROBE_CONFIRM_SAMPLE_COUNT         4U
#endif
#ifndef SLEEP_PROBE_CONFIRM_TIMEOUT_MS
#define SLEEP_PROBE_CONFIRM_TIMEOUT_MS           10UL
#endif
/* Discard probe windows for this long after the probe's first window, so the
 * shutdown/settling transient neither wakes the scale nor becomes the next
 * reference.  The first window after the mask establishes the reference. */
#ifndef SLEEP_PROBE_WAKE_MASK_MS
#define SLEEP_PROBE_WAKE_MASK_MS                 2000UL
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
/* Sample count used by the mass manager's stability statistics during a
 * probe.  The cup-wake decision itself only needs the trigger frame. */
#ifndef SLEEP_PROBE_STABLE_SAMPLE_COUNT
#define SLEEP_PROBE_STABLE_SAMPLE_COUNT         10U
#endif

#if SLEEP_PROBE_FIRST_SAMPLE_TIMEOUT_MS == 0UL
#error "Sleep probe first-sample timeout must be greater than zero"
#endif
#if SLEEP_PROBE_STATE_POLL_MS == 0UL
#error "Sleep probe state poll interval must be greater than zero"
#endif
#if (SLEEP_PROBE_SAMPLE_POLL_MS == 0UL) || \
    (SLEEP_PROBE_TRIGGER_SAMPLE_COUNT == 0U) || \
    ((SLEEP_PROBE_SAMPLE_POLL_MS * SLEEP_PROBE_TRIGGER_SAMPLE_COUNT) > \
     SLEEP_PROBE_FIRST_SAMPLE_TIMEOUT_MS)
#error "Sleep probe timeout cannot collect enough trigger samples"
#endif
#if SLEEP_PROBE_CONFIRM_SAMPLE_COUNT == 0U
#error "Sleep probe confirmation needs at least one frame"
#endif
#if (SLEEP_PROBE_CONFIRM_TIMEOUT_MS == 0UL) || \
    ((SLEEP_PROBE_SAMPLE_POLL_MS * SLEEP_PROBE_CONFIRM_SAMPLE_COUNT) > \
     SLEEP_PROBE_CONFIRM_TIMEOUT_MS)
#error "Sleep probe confirmation timeout cannot collect enough frames"
#endif
#if SLEEP_PROBE_WAKE_MASK_MS == 0UL
#error "Sleep probe wake mask must be greater than zero"
#endif

typedef enum {
    SLEEP_PROBE_EVENT_NONE = 0,
    SLEEP_PROBE_EVENT_CUP_WAKE,
    SLEEP_PROBE_EVENT_ERROR_WAKE,
    SLEEP_PROBE_EVENT_RECOVERED,
} sleep_probe_event_e;

/*
 * The cup-wake decision is relative: the reference keeps the weight measured
 * by the previous probe window.  When a window becomes more than
 * SLEEP_PROBE_WAKE_DELTA_G heavier, its confirmation frames are averaged and
 * only that average is allowed to wake the scale.  This keeps the slow zero
 * drift after power-up from looking like a placed cup, and a cup being
 * removed does not wake the scale.
 */
typedef struct {
    float reference_g;
    uint8_t failure_count;
    bool reference_valid;
    bool error_latched;
} sleep_probe_t;

void sleep_probe_init(sleep_probe_t *probe, bool reference_valid, float reference_g);

sleep_probe_event_e sleep_probe_finish_window(sleep_probe_t *probe,
                                              bool sample_seen,
                                              bool driver_error,
                                              bool measurement_valid,
                                              float window_mass_g);

bool sleep_probe_has_reference(const sleep_probe_t *probe);

float sleep_probe_reference_g(const sleep_probe_t *probe);

uint8_t sleep_probe_failure_count(const sleep_probe_t *probe);

bool sleep_probe_error_latched(const sleep_probe_t *probe);

#ifdef __cplusplus
}
#endif

#endif

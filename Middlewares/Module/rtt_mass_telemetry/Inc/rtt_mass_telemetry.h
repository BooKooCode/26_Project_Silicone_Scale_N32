#ifndef RTT_MASS_TELEMETRY_H
#define RTT_MASS_TELEMETRY_H

#include <stdint.h>

#define RTT_MASS_TELEMETRY_MAX_FRAME_SIZE 192U

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t tick_ms;
    int32_t adc_raw;
    float mass_raw_g;
    float mass_bessel_g;
    float mass_sliding_g;
} rtt_mass_telemetry_sample_t;

void rtt_mass_telemetry_init(void);
void rtt_mass_telemetry_submit(const rtt_mass_telemetry_sample_t *sample);
uint32_t rtt_mass_telemetry_dropped_count(void);

#ifdef __cplusplus
}
#endif

#endif
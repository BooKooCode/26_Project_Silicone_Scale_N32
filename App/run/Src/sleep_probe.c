#include "sleep_probe.h"

void sleep_probe_init(sleep_probe_t *probe, bool baseline_valid, float baseline_g)
{
    probe->baseline_g = baseline_g;
    probe->failure_count = 0U;
    probe->baseline_valid = baseline_valid;
    probe->error_latched = false;
}

sleep_probe_event_e sleep_probe_finish_window(sleep_probe_t *probe,
                                              bool sample_seen,
                                              bool driver_error,
                                              bool stable,
                                              float absolute_mass_g)
{
    if (driver_error || !sample_seen) {
        if (probe->failure_count < UINT8_MAX) {
            probe->failure_count++;
        }
        if (!probe->error_latched && probe->failure_count >= SLEEP_PROBE_FAILURE_LIMIT) {
            probe->error_latched = true;
            return SLEEP_PROBE_EVENT_ERROR_WAKE;
        }
        return SLEEP_PROBE_EVENT_NONE;
    }

    probe->failure_count = 0U;
    if (absolute_mass_g > SLEEP_PROBE_OVERLOAD_LIMIT_G) {
        return SLEEP_PROBE_EVENT_NONE;
    }
    if (probe->error_latched) {
        probe->error_latched = false;
        return SLEEP_PROBE_EVENT_RECOVERED;
    }

    if (!stable) {
        return SLEEP_PROBE_EVENT_NONE;
    }
    if (!probe->baseline_valid) {
        probe->baseline_g = absolute_mass_g;
        probe->baseline_valid = true;
        return SLEEP_PROBE_EVENT_NONE;
    }
    if ((absolute_mass_g - probe->baseline_g) > SLEEP_PROBE_WAKE_DELTA_G) {
        return SLEEP_PROBE_EVENT_CUP_WAKE;
    }
    return SLEEP_PROBE_EVENT_NONE;
}

bool sleep_probe_has_baseline(const sleep_probe_t *probe)
{
    return probe->baseline_valid;
}

float sleep_probe_baseline_g(const sleep_probe_t *probe)
{
    return probe->baseline_g;
}

uint8_t sleep_probe_failure_count(const sleep_probe_t *probe)
{
    return probe->failure_count;
}

bool sleep_probe_error_latched(const sleep_probe_t *probe)
{
    return probe->error_latched;
}
#include "sleep_probe.h"

void sleep_probe_init(sleep_probe_t *probe, bool reference_valid, float reference_g)
{
    probe->reference_g = reference_g;
    probe->failure_count = 0U;
    probe->reference_valid = reference_valid;
    probe->error_latched = false;
}

sleep_probe_event_e sleep_probe_finish_window(sleep_probe_t *probe,
                                              bool sample_seen,
                                              bool driver_error,
                                              bool measurement_valid,
                                              float window_mass_g)
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
    if (window_mass_g > SLEEP_PROBE_OVERLOAD_LIMIT_G) {
        return SLEEP_PROBE_EVENT_NONE;
    }
    if (probe->error_latched) {
        probe->error_latched = false;
        return SLEEP_PROBE_EVENT_RECOVERED;
    }

    /* An unusable window must not move the reference, otherwise a single bad
     * window would silently swallow the next real cup placement. */
    if (!measurement_valid) {
        return SLEEP_PROBE_EVENT_NONE;
    }
    if (!probe->reference_valid) {
        probe->reference_g = window_mass_g;
        probe->reference_valid = true;
        return SLEEP_PROBE_EVENT_NONE;
    }

    float previous_g = probe->reference_g;

    probe->reference_g = window_mass_g;
    if ((window_mass_g - previous_g) > SLEEP_PROBE_WAKE_DELTA_G) {
        return SLEEP_PROBE_EVENT_CUP_WAKE;
    }
    return SLEEP_PROBE_EVENT_NONE;
}

bool sleep_probe_has_reference(const sleep_probe_t *probe)
{
    return probe->reference_valid;
}

float sleep_probe_reference_g(const sleep_probe_t *probe)
{
    return probe->reference_g;
}

uint8_t sleep_probe_failure_count(const sleep_probe_t *probe)
{
    return probe->failure_count;
}

bool sleep_probe_error_latched(const sleep_probe_t *probe)
{
    return probe->error_latched;
}

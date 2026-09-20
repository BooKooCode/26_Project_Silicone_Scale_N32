#include "rtt_mass_telemetry.h"

#include <math.h>
#include <stddef.h>
#include <string.h>
#include "SEGGER_RTT.h"
#include "auto_version.h"

/* Called only by the mass task. Format outside the RTT interrupt lock. */
static uint32_t s_dropped_count;
static uint32_t s_last_info_ms;
static char s_frame[192];

static char *append_u32(char *out, uint32_t value)
{
    char digits[10];
    unsigned count = 0;
    do { digits[count++] = (char)('0' + value % 10U); value /= 10U; } while(value);
    while(count) { *out++ = digits[--count]; }
    return out;
}

static char *append_i32(char *out, int32_t value)
{
    if(value < 0) { *out++ = '-'; return append_u32(out, 0U - (uint32_t)value); }
    return append_u32(out, (uint32_t)value);
}

static char *append_mass(char *out, float value)
{
    /* Avoid printf float support and its large stack cost on Cortex-M0+. */
    if(!isfinite(value) || value > 2000000.0f || value < -2000000.0f) {
        memcpy(out, "nan", 3); return out + 3;
    }
    if(value < 0.0f) { *out++ = '-'; value = -value; }
    uint32_t milli = (uint32_t)(value * 1000.0f + 0.5f);
    out = append_u32(out, milli / 1000U);
    *out++ = '.';
    *out++ = (char)('0' + (milli / 100U) % 10U);
    *out++ = (char)('0' + (milli / 10U) % 10U);
    *out++ = (char)('0' + milli % 10U);
    return out;
}

static void send_frame(const char *end)
{
    unsigned length = (unsigned)(end - s_frame);
    if(SEGGER_RTT_Write(0, s_frame, length) != length) { ++s_dropped_count; }
}

void rtt_mass_telemetry_init(void)
{
    s_dropped_count = 0U;
    s_last_info_ms = UINT32_MAX - 1000U;
    (void)SEGGER_RTT_SetFlagsUpBuffer(0, SEGGER_RTT_MODE_NO_BLOCK_SKIP);
}

void rtt_mass_telemetry_submit(const rtt_mass_telemetry_sample_t *sample)
{
    if(sample == NULL) { return; }
    char *out = s_frame;
    memcpy(out, "@RTT,weight,", 12); out += 12;
    out = append_i32(out, sample->adc_raw); *out++ = ',';
    out = append_mass(out, sample->mass_raw_g); *out++ = ',';
    out = append_mass(out, sample->mass_bessel_g); *out++ = ',';
    out = append_mass(out, sample->mass_sliding_g); *out++ = '\n';
    send_frame(out);

    /* Repeated metadata allows attaching after boot, without downlink queries. */
    if((uint32_t)(sample->tick_ms - s_last_info_ms) >= 1000U) {
        s_last_info_ms = sample->tick_ms;
        out = s_frame;
        memcpy(out, "@RTT,status,", 12); out += 12;
        out = append_u32(out, sample->tick_ms); *out++ = ',';
        out = append_u32(out, s_dropped_count); *out++ = '\n';
        send_frame(out);
        out = s_frame;
        memcpy(out, "@RTT,version,", 13); out += 13;
        for(unsigned i = 0; i < _MAX_LEN_ && TAG_VERSION[i]; ++i) {
            char c = TAG_VERSION[i];
            *out++ = (c < 32 || c == ',' || c == 127) ? '_' : c;
        }
        *out++ = '\n';
        send_frame(out);
    }
}

uint32_t rtt_mass_telemetry_dropped_count(void)
{
    return s_dropped_count;
}

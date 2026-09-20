#ifndef CORE_RTC_H
#define CORE_RTC_H

#include <stdbool.h>
#include <stdint.h>

#define RTC_SUBTICKS_PER_SECOND 256U
#define RTC_WAKEUP_CLOCK_HZ (32768U / 16U)
#define RTC_WAKEUP_COUNTER_MAX 0x10000UL

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RTC_STATUS_NOT_INITIALIZED = 0,
    RTC_STATUS_READY,
    RTC_STATUS_LSE_TIMEOUT,
    RTC_STATUS_INIT_FAILED,
    RTC_STATUS_SYNC_FAILED,
    RTC_STATUS_WAKEUP_FAILED,
    RTC_STATUS_LSE_LOST,
    RTC_STATUS_READ_FAILED
} rtc_status_t;

void NS_RTC_Init(void);
rtc_status_t rtc_get_status(void);
bool rtc_is_ready(void);
bool rtc_capture_subticks(uint32_t *subticks);
bool rtc_arm_wakeup(uint32_t counter);
bool rtc_disarm_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif
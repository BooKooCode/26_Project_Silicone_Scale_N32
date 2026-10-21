#ifndef __BAT_MEAS_H__
#define __BAT_MEAS_H__

#include "bookoo_error_def.h"
#include <stdint.h>


/* Minimum spacing between the one-shot battery refreshes performed during the
 * cup-wakeup stand-by probe.  The probe itself wakes every 2 s, but sampling
 * the battery on every window would keep the divider and the ADC active for a
 * large fraction of the stand-by time.  30 s is short enough to notice an
 * empty battery while keeping the average current close to the STOP2 level. */
#ifndef BAT_SLEEP_REFRESH_INTERVAL_MS
#define BAT_SLEEP_REFRESH_INTERVAL_MS   30000UL
#endif


SYSTEM_ERROR_CODE_E start_bat_sample(void);

SYSTEM_ERROR_CODE_E stop_bat_sample(void);

/* One-shot refresh used by the stand-by probe: only valid while the system is
 * in SLEEPING and not charging. */
SYSTEM_ERROR_CODE_E bat_sleep_refresh_start(void);

void bat_gpio_exti_handler(uint16_t GPIO_Pin);

#endif

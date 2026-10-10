#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include "main.h"

#ifndef SLEEP_LEFT_KEY_WAKE_ENABLED
#define SLEEP_LEFT_KEY_WAKE_ENABLED 0U
#endif

#if (SLEEP_LEFT_KEY_WAKE_ENABLED != 0U) && (SLEEP_LEFT_KEY_WAKE_ENABLED != 1U)
#error "SLEEP_LEFT_KEY_WAKE_ENABLED must be 0 or 1"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void NS_GPIO_Init(void);
void gpio_sleep_left_wakeup_enable(void);
void gpio_sleep_left_wakeup_disable(void);
void gpio_sleep_left_wakeup_arm_if_released(void);
bool gpio_sleep_left_wakeup_is_enabled(void);
bool gpio_sleep_left_wakeup_wait_ready(void);
bool gpio_sleep_left_wakeup_pending(void);
bool gpio_sleep_left_wakeup_capture_level(void);

#ifdef __cplusplus
}
#endif

#endif

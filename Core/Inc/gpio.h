#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

void NS_GPIO_Init(void);
void gpio_sleep_left_wakeup_enable(void);
void gpio_sleep_left_wakeup_disable(void);
void gpio_sleep_left_wakeup_arm_if_released(void);
bool gpio_sleep_left_wakeup_is_enabled(void);
bool gpio_sleep_left_wakeup_pending(void);
bool gpio_sleep_left_wakeup_capture_level(void);

#ifdef __cplusplus
}
#endif

#endif

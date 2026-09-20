#ifndef __BAT_MEAS_H__
#define __BAT_MEAS_H__

#include "bookoo_error_def.h"
#include <stdint.h>


SYSTEM_ERROR_CODE_E start_bat_sample(void);

SYSTEM_ERROR_CODE_E stop_bat_sample(void);

void bat_gpio_exti_handler(uint16_t GPIO_Pin);

#endif

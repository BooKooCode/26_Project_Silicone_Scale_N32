#ifndef CORE_TIM_H
#define CORE_TIM_H

#include "main.h"

#define MEAS_RUNTIME_TIM TIM7
#define HW_DELAY_US_DWT

#ifdef __cplusplus
extern "C" {
#endif

void NS_TIM7_Init(void);
void NS_TIM1_Init(void);
void NS_TIM6_Init(void);

#ifdef __cplusplus
}
#endif

#endif
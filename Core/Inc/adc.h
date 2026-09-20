#ifndef CORE_ADC_H
#define CORE_ADC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void NS_ADC1_Init(void);
bool adc_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif
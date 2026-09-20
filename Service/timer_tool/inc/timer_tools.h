#ifndef __TIMER_TOOLS_H__
#define __TIMER_TOOLS_H__


#include "stdint.h"


void meas_tim_init(void);

void meas_tim_irq_handler(void);

void start_meas_tim(void);

uint32_t get_meas_tim_dus(void);

uint32_t cut_meas_tim_dus(void);


void hw_delay_tim_init(void);

void hw_delay_us(uint16_t us);

#endif

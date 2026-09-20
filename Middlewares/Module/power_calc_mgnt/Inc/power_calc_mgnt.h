#ifndef POWER_CALC_MGNT_H__
#define POWER_CALC_MGNT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "bookoo_error_def.h"


void power_calc_mgnt_init(void);

SYSTEM_ERROR_CODE_E power_calc_mgnt_process(float const _volt_input);

void power_calc_mgnt_reset(void);

uint8_t get_power_cal_mgnt_record_size(void);

float get_power_cal_mgnt_mean_vol(void);

float get_power_cal_mgnt_percent_fp32(void);
    
uint8_t get_power_cal_mgnt_percent_u8(void);


#ifdef __cplusplus
}
#endif

#endif /* POWER_CALC_MGNT_H__ */
/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include <math.h>
#include <string.h>

#include "app_error.h"
#include "power_calc_mgnt.h"

/* Defines -------------------------------------------------------------------*/
#define VOL_LEVEL_COUNT			        11U         /* Size of the power voltage table */
#define VOL_CAL_FILTER_SIZE             5U          /* Size of the pecentage filter */


/* Private variables ---------------------------------------------------------*/
static const float s_volt_table[] = {
	4.17f,  4.06f,  3.95f,  3.86f,  3.79f,  3.71f,  3.65f,  3.62f,  3.60f,  3.55f,  3.46f
};

static const float s_percentage_table[] = {
    100,    90,     80,     70,     60,     50,     40,     30,     20,     10,     0
};


typedef struct {
    uint8_t record_index;                           /* Index of the battery voltage cache */
    float vol_cache[VOL_CAL_FILTER_SIZE];           /* Battery voltage cache buffer */
	float vol_mean;                                 /* Battery voltage mean value */
    
	float percent_fp32;
	uint8_t percent_u8;
} power_cal_mgnt_t;


static power_cal_mgnt_t s_power_cal_mgnt;

/* Private function declarations ---------------------------------------------*/

void power_calc_mgnt_init(void) {
	ASSERT(sizeof(s_volt_table) != 0);
	ASSERT(sizeof(s_volt_table) == sizeof(s_percentage_table));
    memset(&s_power_cal_mgnt, 0, sizeof(power_cal_mgnt_t));
}


SYSTEM_ERROR_CODE_E power_calc_mgnt_process(float const _volt_input) {
    if(s_power_cal_mgnt.record_index >= VOL_CAL_FILTER_SIZE) {
        return ERR_RESOURCE_FULL;
    }
	s_power_cal_mgnt.vol_cache[s_power_cal_mgnt.record_index] = _volt_input;
	s_power_cal_mgnt.record_index ++;
	/* Voltage buffer full */
	if(s_power_cal_mgnt.record_index < VOL_CAL_FILTER_SIZE) {
        return ERR_FAIL;
	}
    
    /* Mean value of the voltage */
    float temp = 0.0f;
    for(uint8_t i = 0; i < VOL_CAL_FILTER_SIZE; i++) {
        temp += s_power_cal_mgnt.vol_cache[i];
    }
    temp /= (float)VOL_CAL_FILTER_SIZE;
    s_power_cal_mgnt.vol_mean = temp;
    
    /* Percentage calculation */
    if(temp > s_volt_table[0]) {
        s_power_cal_mgnt.percent_fp32 = s_percentage_table[0];
    }
    else if(temp < s_volt_table[VOL_LEVEL_COUNT - 1]) {
        s_power_cal_mgnt.percent_fp32 = s_percentage_table[VOL_LEVEL_COUNT - 1];
    }
    else {
        for(uint8_t i = 1; i < VOL_LEVEL_COUNT; i++) {
            if(temp > s_volt_table[i]) {
                s_power_cal_mgnt.percent_fp32 = s_percentage_table[i - 1];
                break;
            }
        }
    }
    s_power_cal_mgnt.percent_u8 = (uint8_t)s_power_cal_mgnt.percent_fp32;
    return ERR_NONE;
}


void power_calc_mgnt_reset(void) {
	s_power_cal_mgnt.record_index = 0;
    memset(&s_power_cal_mgnt.vol_cache, 0, sizeof(s_power_cal_mgnt.vol_cache));
}


uint8_t get_power_cal_mgnt_record_size(void) {
    return VOL_CAL_FILTER_SIZE;
}


float get_power_cal_mgnt_mean_vol(void) {
    return s_power_cal_mgnt.vol_mean;
}


float get_power_cal_mgnt_percent_fp32(void) {
    return s_power_cal_mgnt.percent_fp32;
}


uint8_t get_power_cal_mgnt_percent_u8(void) {
    return s_power_cal_mgnt.percent_u8;
}


/**
 * @}
 */
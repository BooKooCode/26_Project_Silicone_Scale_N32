#ifndef __DEV_TM1640B_H__
#define __DEV_TM1640B_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "ns_error.h"
	
/**
 * @defgroup nrf_dev_tm1640b
 * @{
 * @ingroup nrf_dev_tm1640b
 * @brief   TM1640B driver depends on the SPIM peripheral. 
 */
	
#define DISCTRL_DIS_MAX_BRIGHT	0x0F
#define DISCTRL_DIS_MIN_BRIGHT	0x00

/**
 * @brief Function for tm1640b initialize.
 *
 * @retval None.
 */
void dev_tm1640b_init(void);
	
/**
 * @brief Function for turning the display ON/OFF.
 *
 * @retval NS_SUCCESS              The ON/OFF control was successful.
 * @retval NS_ERROR_INVALID_STATE  The driver is sleeping.
 */
ret_code_t dev_tm1640b_onoff_ctrl(bool _flag);

/**
 * @brief Function for setting the tm1640b to sleep.
 *
 * @retval None.
 */
void dev_tm1640b_sleeping(void);

/**
 * @brief Function for wakeup the tm1640.
 *
 * @retval NS_SUCCESS              The wakeup was successful.
 * @retval NS_ERROR_INVALID_STATE  The wakeup sequence failed.
 */
ret_code_t dev_tm1640b_wakeup(void);

/**
 * @brief Function for send data to the tm1640b.
 *
 * @retval NS_SUCCESS              The data write was successful.
 * @retval NS_ERROR_INVALID_STATE  The driver is sleeping.
 * @retval NS_ERROR_INVALID_PARAM  The input arguments are invalid.
 */
ret_code_t dev_tm1640b_senddata(uint8_t _start_addr, uint8_t const * _pdata, uint8_t _len);

/**
 * @brief Function for setting the brightness of tm1640b.
 *
 * @retval None.
 */
void dev_tm1640b_setbrightness(uint8_t _brightness);

bool dev_tm1640b_in_sleeping(void);

void dev_tm1640b_init_software(void);
void dev_tm1640b_onoff_ctrl_software(bool _flag);
void dev_tm1640b_wakeup_software(void);
void dev_tm1640b_senddata_software(uint8_t _start_addr, uint8_t const * _pdata, uint8_t _len);

/** @} */

#ifdef __cplusplus
}
#endif

#endif	/* NRF_DEV_TM1640B_H__ */

#ifndef __SAADC_MGNT_H__
#define __SAADC_MGNT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "bookoo_error_def.h"

/**
 * @defgroup saadc_mgnt SAADC manager
 * @{
 * @ingroup saadc_mgnt
 * @brief   SAADC manager with block sampling. Channel gain 1/6, internal reference
 */

/** @brief Data structure of the SAADC manager pin control for lowpower consumption. */
typedef struct
{
	bool pin_ctrl_enable;
	bool active_level;
	uint32_t pin;
} saadc_mgnt_pin_ctrl_t;

/** @brief Data structure of the pins managed by the SAADC manager. */
typedef struct
{
	uint32_t AINx;
	saadc_mgnt_pin_ctrl_t pin_ctrl;
} saadc_mgnt_pin_t;

/**
 * @brief Function for initializing the SAADC manager
 *
 * This function configures the specified peripheral and sampling channels.
 *
 * @param[in] _mgnt_pins Pointer to the pins managed by the SAADC manager
 * @param[in] _num       Number of the pins
 *
 * @retval None
 */
void saadc_mgnt_init(saadc_mgnt_pin_t * const _mgnt_pins, const uint32_t _num);

/**
 * @brief Function for enabling the SAADC manager
 *
 * This function enables the SAADC manager.
 *
 * @param[in] None
 *
 * @retval None
 */
void saadc_mgnt_enable(void);

/**
 * @brief Function for disabling the SAADC manager
 *
 * This function enables the SAADC manager.
 *
 * @param[in] None
 *
 * @retval None
 */
void saadc_mgnt_disable(void);

/**
 * @brief Function for sampling in SAADC blocking mode.
 *
 * This function enables a single converion in SAADC blocking mode
 *
 * @param[out] _volt_arr Pointer to the array that storages the sampling result
 *
 * @retval ERR_NONE           The conversion was successful.
 * @retval ERR_INVALID_STATE  The SAADC manager is disabled.
 * @retval ERR_TIMEOUT        The conversion timed out.
 */
SYSTEM_ERROR_CODE_E saadc_mgnt_sample_blocking(float * const _volt_arr);


/** @} */

#ifdef __cplusplus
}
#endif

#endif	/* __SAADC_MGNT_H__ */

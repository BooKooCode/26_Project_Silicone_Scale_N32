#ifndef __DEV_SFUD_MGNT_H__
#define __DEV_SFUD_MGNT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ns_error.h"

/**
 * @defgroup dev_sfud_mgnt
 * @{
 * @ingroup dev_sfud_mgnt
 * @brief   SFUD manager for SPIM FLASH.
 */
	
/**
 * @brief Function for SFUD manager initialize.
 *
 * @retval NS_SUCCESS                  The initialization was successful.
 * @retval NS_ERROR_INVALID_STATE      The initialization failed (sfud_init() failed).
 */
ret_code_t dev_sfud_mgnt_init(void);

/**
 * @brief Function for reading the data from the SPIM FLASH.
 *
 * @retval NS_SUCCESS                  The read operation was successful.
 * @retval NS_ERROR_BUSY               The read operation failed (sfud_read() failed).
 * @retval NS_ERROR_INVALID_STATE      The flash is not initialized.
 */
ret_code_t dev_sfud_mgnt_read(uint32_t addr, uint32_t size, uint8_t *data);
ret_code_t dev_sfud_mgnt_write(uint32_t addr, uint32_t size, uint8_t *data);
ret_code_t dev_sfud_mgnt_erasewrite(uint32_t addr, uint32_t size, uint8_t *data);
ret_code_t dev_sfud_mgnt_erase(uint32_t addr, uint32_t size);

/** @} */

#ifdef __cplusplus
}
#endif

#endif	/* NRF_DEV_SFUD_MGNT_H__ */
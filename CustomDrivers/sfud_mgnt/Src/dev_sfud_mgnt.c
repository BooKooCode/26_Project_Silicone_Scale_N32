/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "resource_occupation.h"

#if NRFX_CHECK(DEV_SFUD_ENABLED)

#include "dev_sfud_mgnt.h"
#include "bookoo_error_def.h"

#include <sfud.h>

#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
#include "log.h"
#define SFUD_MGNT_LOG_WARNING(...)               \
	do {                                          \
		(void)LOG_WARNING("sfud_mgnt", __VA_ARGS__); \
	} while(0)
#endif

/* Defines -------------------------------------------------------------------*/
const sfud_flash * default_flash = NULL;
const user_data_spix_t * user_data_spix = NULL;

/* Private variables ---------------------------------------------------------*/

/* Private function declarations ---------------------------------------------*/
/* Function prototypes -------------------------------------------------------*/
/**@brief Function Brief.
 *
 * @param[in]   xxx   Parameter description
 * @param[out]  xxx   Parameter description
 */
ret_code_t dev_sfud_mgnt_init(void) {
	if(default_flash != NULL) {
		if(default_flash->init_ok) {
			return NS_SUCCESS;
		}
	}
	default_flash = sfud_get_device_table() + 0;
	if(default_flash == NULL) {
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_NOT_FOUND;
	}
	
	sfud_err sfud_err_code = sfud_init();
	if(sfud_err_code != SFUD_SUCCESS){
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
		SFUD_MGNT_LOG_WARNING("SFUD initialize failed! error code: 0x%08X", sfud_err_code);
#endif
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL + sfud_err_code);
		return NS_ERROR_INVALID_STATE;
	}
	user_data_spix = default_flash->spi.user_data;
	if(user_data_spix == NULL) {
		return NS_ERROR_NOT_FOUND;
	}
	
	return NS_SUCCESS;
}


ret_code_t dev_sfud_mgnt_read(uint32_t addr, uint32_t size, uint8_t *data) {
	if(default_flash == NULL) {
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_INVALID_STATE;
	}
	
	sfud_err sfud_err_code = SFUD_SUCCESS;
	
	if(default_flash->init_ok) {
		sfud_err_code = sfud_read(default_flash, addr, size, data);
		
		if(sfud_err_code != SFUD_SUCCESS) {
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
			SFUD_MGNT_LOG_WARNING("SFUD FLASH read failed, error code: 0x%08X", sfud_err_code);
#endif
			//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_READ_FAIL + sfud_err_code);
			return NS_ERROR_BUSY;
		}
	}
	else {
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
		SFUD_MGNT_LOG_WARNING("SFUD FLASH invalied state, read operation failed");
#endif
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_INVALID_STATE;
	}
	return NS_SUCCESS;
}


ret_code_t dev_sfud_mgnt_write(uint32_t addr, uint32_t size, uint8_t *data) {
	if(default_flash == NULL) {
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_INVALID_STATE;
	}
	
	sfud_err sfud_err_code = SFUD_SUCCESS;
	
	if(default_flash->init_ok) {
		sfud_err_code = sfud_write(default_flash, addr, size, data);
		
		if(sfud_err_code != SFUD_SUCCESS) {
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
			SFUD_MGNT_LOG_WARNING("SFUD FLASH write failed, error code: 0x%08X", sfud_err_code);
#endif
			//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_WRITE_FAIL + sfud_err_code);
			return NS_ERROR_BUSY;
		}
	}
	else {
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
		SFUD_MGNT_LOG_WARNING("SFUD FLASH invalied state, write operation failed");
#endif
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_INVALID_STATE;
	}
	return NS_SUCCESS;
}


ret_code_t dev_sfud_mgnt_erasewrite(uint32_t addr, uint32_t size, uint8_t *data) {
	if(default_flash == NULL) {
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_INVALID_STATE;
	}

	sfud_err sfud_err_code = SFUD_SUCCESS;
	
	if(default_flash->init_ok) {
		sfud_err_code = sfud_erase_write(default_flash, addr, size, data);
		
		if(sfud_err_code != SFUD_SUCCESS) {
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
			SFUD_MGNT_LOG_WARNING("SFUD FLASH erase write failed, error code: 0x%08X", sfud_err_code);
#endif
			//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_ERASEWRITE_FAIL + sfud_err_code);
			return NS_ERROR_BUSY;
		}
	}
	else {
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
		SFUD_MGNT_LOG_WARNING("SFUD FLASH invalied state, erase write operation failed");
#endif
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_INVALID_STATE;
	}
	return NS_SUCCESS;
}


ret_code_t dev_sfud_mgnt_erase(uint32_t addr, uint32_t size) {
	if(default_flash == NULL) {
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_INVALID_STATE;
	}

	sfud_err sfud_err_code = SFUD_SUCCESS;
	
	if(default_flash->init_ok) {
		sfud_err_code = sfud_erase(default_flash, addr, size);
		
		if(sfud_err_code != SFUD_SUCCESS) {
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
			SFUD_MGNT_LOG_WARNING("SFUD FLASH erase failed, error code: 0x%08X", sfud_err_code);
#endif
			//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_ERASE_FAIL + sfud_err_code);
			return NS_ERROR_BUSY;
		}
	}
	else {
#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
		SFUD_MGNT_LOG_WARNING("SFUD FLASH invalied state, erase operation failed");
#endif
		//APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL);
		return NS_ERROR_INVALID_STATE;
	}
	return NS_SUCCESS;
}
#endif
/**
 * @}
 */
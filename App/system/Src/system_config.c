/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include <stdio.h>

#include "app.h"
#include "dev_status.h"
#include "dev_config.h"

#include "gpio.h"
#include "auto_version.h"
#include "log.h"

#include "dev_tm1640b.h"
#include "os_mgnt.h"

#if defined(__has_include)
#if __has_include("cm_backtrace.h")
#include "cm_backtrace.h"
#define SYSTEM_CONFIG_HAS_BACKTRACE 1
#endif
#if __has_include("app_timer.h")
#include "app_timer.h"
#endif
#endif

#ifndef SYSTEM_CONFIG_HAS_BACKTRACE
#define SYSTEM_CONFIG_HAS_BACKTRACE 0
#endif

/* Defines -------------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/


/* Private function declarations ---------------------------------------------*/


static uint32_t parse_version_code_from_tag(const char *tag) {
	unsigned int major = 0U;
	unsigned int minor = 0U;
	unsigned int patch = 0U;
	if((tag == NULL) || (sscanf(tag, "v%u.%u.%u", &major, &minor, &patch) != 3)) {
		return 0;
	}
	if(major > 9U) {
		major = 9U;
	}
	minor %= 10U;
	patch %= 10U;
	return (major * 100U) + (minor * 10U) + patch;
}


static void dev_dfu_init(void) {
	uint32_t temp_ver = parse_version_code_from_tag(TAG_VERSION);
	if(temp_ver == 0U) {
		APP_WARNING_HANDLER(BOOKOO_WARNING_VER_UNKNOW);
	}
	dev_var_set(SOFT_VERSION_INDEX, (FORMAT_4BYTES_U *)&temp_ver);
	static char SOFTWARE_VERSION[] = "Vx.x.x";
	unsigned int major = (unsigned int)(temp_ver / 100U);
	unsigned int minor = (unsigned int)((temp_ver % 100U) / 10U);
	unsigned int patch = (unsigned int)((temp_ver % 100U) % 10U);
	SOFTWARE_VERSION[1] = (char)('0' + major);
	SOFTWARE_VERSION[3] = (char)('0' + minor);
	SOFTWARE_VERSION[5] = (char)('0' + patch);
	LOG_INFO("sys_config", "App version: %u.%u.%u", major, minor, patch);
}


/* Function prototypes -------------------------------------------------------*/
/**@brief Function Brief.
 *
 * @param[in]   xxx   Parameter description
 * @param[out]  xxx   Parameter description
 */
/* danger tag: driver function */
void system_device_init(void) {
    /* Device status cache init */
    dev_status_init();
	/* SFUD and UICR init */
	dev_store_cfg_init();
    /* Dfu init */
    dev_dfu_init();
	
	/* Backtrace initialize */
//	cm_backtrace_init("Backtrace", HARDWARE_VERSION, SOFTWARE_VERSION);
}


void system_device_init_dependSD(void) {
	/* Initialize system resources depend on Nordic SoftDevice */
	return;
}


void system_task_init(void) {
	os_mgnt_init();
}

/**
 * @}
 */
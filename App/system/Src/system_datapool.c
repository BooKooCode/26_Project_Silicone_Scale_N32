/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "internal.h"

/* Defines -------------------------------------------------------------------*/

/* RTOS Resources ------------------------------------------------------------*/
/* Queues */
xQueueHandle cus_rx_queue = NULL;
xQueueHandle cus_tx_queue = NULL;
xQueueHandle state_req_queue = NULL;
xQueueHandle timing_req_queue = NULL;

/* Semaphores */
/* Mutexes */
/* Notifications */
/* Timer */
TimerHandle_t bat_sample_timer = NULL;
bool bat_sample_timer_stop = true;
uint32_t bat_sample_repect_cnt = 0;

/* Other Resources -----------------------------------------------------------*/
/* System manage */
system_state_e sys_state = SLEEPING;
system_on_off_e sys_on_off = SYS_OFF;
uint32_t sys_standby_cnt = 0;
ble_dfu_APPversion_s version_info = {0};
uicr_user_data_s uicr_user_data;
power_calc_mgnt_result_s power_calc_res = {0};

/* Sensoring values */
uint8_t battery_level = 0;
mass_mgnt_datapack_s mass = {0};
mass_mgnt_datapack_s flowrate = {0};
//mass_mgnt_datapack_s ratio = {0};
//mass_mgnt_datapack_s powder = {0};
uint8_t cnt = 0;
/* 
	This resource does not need to be mutually exclusive 
	as its writing operation only occurs at the transport protocol layer
*/
user_config_data_s user_config = {0};


/* Action counting variables for auto mode */
uint32_t auto_mode_switchcnt = 0;
bool auto_mode_endreq = false;



/* Lastest results for auto mode */
float auto_mode_lastest_mass = 0.0f;
float auto_mode_lastest_flowrate_avg = 0.0f;
uint32_t auto_mode_lastest_cnt_ms = 0;


/* Low battery charging logic */
bool lowpower_protect_bandbtn = false;
bool incharging_flag = false;
bool finishcharging_detect = false;

/* If a extraction start command from the APP */
uint8_t app_extractionstart_cmd = 0;

/* If in extraction start peeling */
//bool app_extractionstart_peeling = false;

/* Mass to be send */
float app_mass_afterpeeling = 0.0;

/**
 * @}
 */
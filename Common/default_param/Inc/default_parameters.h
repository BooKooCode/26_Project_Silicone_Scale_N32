#ifndef __DEFAULT_PARAMETERS_H__
#define __DEFAULT_PARAMETERS_H__

#include "dev_status.h"


#define PARAM_KEY_STANDBY_MIN           "standby_min"
#define PARAM_KEY_MASS_CALI_K           "mass_cali_k"
#define PARAM_KEY_SENSOR_TYPE           "sensor_type"
#define PARAM_KEY_MASS_UNIT             "mass_unit"
#define PARAM_KEY_SOUND_GEAR            "sound_gear"
#define PARAM_KEY_INIT_MODE             "init_mode"
#define PARAM_KEY_SMOOTH_ENABLE         "smooth_enable"
#define PARAM_KEY_AUTO_STOP             "auto_stop_method"
#define PARAM_KEY_SOUND_ENABLE          "sound_enable"
#define PARAM_KEY_MODE_MASK             "mode_mask"
#define PARAM_KEY_ANI_CYCLE             "animation_cycle"
#define PARAM_KEY_TOUCH_GUARD           "touch_guard"

#define STRING_KEY_DEV_NAME             "dev_name"


#define TASK_FSM_INTERVAL_MS		    100
#define TASK_FSM_REQ_INTERVAL_MIN_MS    50
#define TASK_STORE_INTERVAL_MS		    100
#define TASK_MASS_INTERVAL_MS		    25
#define TASK_DISP_INTERVAL_MS		    25
#define TASK_BAT_INTERVAL_MS		    2000
#define TASK_TIMINGREQ_INTERVAL_MS 		100
#define TASK_REGULAR_SLEEP_INTERVAL_MS  1800
#define TASK_REGULAR_WAKEUP_INTERVAL_MS 100
#define TASK_DEBUG_VOFA_INTERVAL_MS     25

#define TIM_BAT_SAMPLE_PERIOD_MS        10

#define AUTO_MODE_PEELING_MS            1000
#define AUTO_MODE_PEELING_COUNT         (AUTO_MODE_PEELING_MS / TASK_FSM_INTERVAL_MS)

#define AUTO_MODE_CUP_LOWEST_GRAM       5.0f
#define AUTO_MODE_STOP_LOWEST_GRAM      15.0f


#define DE_CFG_DESIRE_MASS_G			36.0f
#define DE_CFG_STANDBY_TIME				15.0f
#define DE_CFG_MASS_CALI_K				-1.0f
#define DE_CFG_MASS_UNIT                MASS_UNIT_G
#define DE_CFG_BUZZER_GEAR				BUZZER_GEAR_LOW
#define DE_CFG_INIT_MODE                TIMING_MODE
#define DE_SMOOTH_ENABLE	            false
#define DE_ANI_CYCLE_MS                 80

#define BTN_RIGHT_PIN                   19
#define BTN_LEFT_PIN                    20

#define HW_POWERON_PIN                  7

#define BAT_AIN                         0
#define BAT_SAMPLE_ENABLE_PIN			4

#define CHARGING_ENABLE_PIN				8
#define FULL_CHARGED_PIN				2

#endif

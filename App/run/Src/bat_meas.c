#include "main.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "app_error.h"
#include "os_mgnt.h"
#include "dev_status.h"
#include "dev_config.h"
#include "saadc_mgnt.h"
#include "power_calc_mgnt.h"
#include "scale_digits_display.h"
#include "bat_meas.h"
#include "hci.h"


#define IS_INCHARGE_TRIGGER()               (GPIO_ReadInputDataBit(CHARGING_ENABLE_GPIO_Port, CHARGING_ENABLE_Pin) == 0U)
#define IS_FULL_CHARGE_TRIGGER()            (GPIO_ReadInputDataBit(FULL_CHARGED_GPIO_Port, FULL_CHARGED_Pin) == 0U)


static TimerHandle_t s_sample_timer = NULL;
static os_task_info_t *s_bat_os = NULL;
static bool s_sample_finished = true;
static bool s_full_charge_reported = false;
static SemaphoreHandle_t s_sample_mutex = NULL;


saadc_mgnt_pin_t saadc_cfgs[1] = {
    {
		.AINx = BAT_AIN,
		.pin_ctrl.pin_ctrl_enable = true,
		.pin_ctrl.active_level = 1,
		.pin_ctrl.pin = BAT_SAMPLE_ENABLE_PIN,
	}
};


static void bat_sample_timeout_handler(TimerHandle_t xTimer) {
	float bat_volt;
    uint8_t bat_percent = 0;
    bool is_sample_finished = false;
    bool is_sample_failed = false;
    bool is_timer_stop_failed = false;

	configASSERT(xTimer);
	if((s_sample_mutex == NULL) || (xSemaphoreTake(s_sample_mutex, 0) != pdTRUE)) {
		return;
	}
	/* Check if the timer has been stopped */
	if(s_sample_finished) {
		xSemaphoreGive(s_sample_mutex);
		return;
	}
    /* Sampling */
    if(saadc_mgnt_sample_blocking(&bat_volt) != ERR_NONE) {
        power_calc_mgnt_reset();
        s_sample_finished = true;
        if(xTimerStop(xTimer, 0) != pdPASS) {
            is_timer_stop_failed = true;
        }
        saadc_mgnt_disable();
        is_sample_failed = true;
    }
    else {
        /* Calculate */
        SYSTEM_ERROR_CODE_E _cal_ret = power_calc_mgnt_process(bat_volt * 2.0f);
        /* Data full band sampling */
        if(_cal_ret == ERR_NONE || _cal_ret == ERR_RESOURCE_FULL) {
            bat_percent = get_power_cal_mgnt_percent_u8();
            /* Repect count reached, close the timer */
            s_sample_finished = true;
            if(xTimerStop(xTimer, 0) != pdPASS) {
                is_timer_stop_failed = true;
            }
            saadc_mgnt_disable();
            is_sample_finished = true;
        }
    }
	xSemaphoreGive(s_sample_mutex);

    if(is_timer_stop_failed) {
        APP_ERROR_HANDLER(ERR_BUSY);
    }
    if(is_sample_failed) {
        APP_WARNING_HANDLER(BOOKOO_WARNING_BAT_SAMPLE_BUSY);
        return;
    }
    if(is_sample_finished) {
        FORMAT_4BYTES_U _value = {0};
        _value.us8[0] = bat_percent;
        dev_var_set(POWER_PERCENT_INDEX, &_value);

        uint32_t _is_incharging = false;
        dev_status_get(INCHARGING_STATE, &_is_incharging);

        if(false == _is_incharging) {
            /* Low power protection request */
            if(_value.us8[0] == 0) {
                uint8_t FSM_cob = SYS_LOWPOWER_PROTECT_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            /* Low power warning indication */
            else if(_value.us8[0] <= 10) {
                digits_disp_req(DISP_LOWPOWER_INTOWARNING_REQ);
            }
            /* No indication */
            else {
                digits_disp_req(DISP_LOWPOWER_EXITWARNING_REQ);
            }
        }
    }
}


static void incharging_evt_handler(void) {
	uint8_t FSM_cob;
	/* Insert the cable, low level */
	power_calc_mgnt_reset();
	
	if(IS_INCHARGE_TRIGGER()) {
		/* Incharge Trigger */
		FSM_cob = SYS_INCHARGING_REQ;
        send_fsm_critical_request(FSM_cob);

		if(IS_FULL_CHARGE_TRIGGER()) {
			FSM_cob = SYS_FULL_CHARGED_REQ;
            send_fsm_critical_request(FSM_cob);
            s_full_charge_reported = true;
		}
        else {
            s_full_charge_reported = false;
        }
	}
	else {
        s_full_charge_reported = false;
    FSM_cob = SYS_EXITCHARGING_REQ;
    send_fsm_critical_request(FSM_cob);
	}
}


static void full_charged_poll(void) {
    uint32_t _is_incharging = false;
    uint8_t FSM_cob;
    dev_status_get(INCHARGING_STATE, &_is_incharging);

    if(_is_incharging && IS_INCHARGE_TRIGGER() && IS_FULL_CHARGE_TRIGGER() &&
       !s_full_charge_reported) {
        /* Full charge trigger, active low while cable is still inserted. */
        FSM_cob = SYS_FULL_CHARGED_REQ;
		send_fsm_critical_request(FSM_cob);
		s_full_charge_reported = true;
    }
	else if(!IS_FULL_CHARGE_TRIGGER()) {
		s_full_charge_reported = false;
	}
}


void bat_gpio_exti_handler(uint16_t GPIO_Pin) {
    if(GPIO_Pin == CHARGING_ENABLE_Pin) {
        incharging_evt_handler();
    }
}


SYSTEM_ERROR_CODE_E start_bat_sample(void) {
    if((NULL == s_sample_timer) || (NULL == s_sample_mutex)) {
        return ERR_NOT_INIT;
    }
    if(xSemaphoreTake(s_sample_mutex, portMAX_DELAY) != pdTRUE) {
        return ERR_FAIL;
    }
    if(false == s_sample_finished) {
        xSemaphoreGive(s_sample_mutex);
        return ERR_FAIL;
    }
    s_sample_finished = false;
    power_calc_mgnt_reset();
    saadc_mgnt_enable();
    if(xTimerReset(s_sample_timer, 0) != pdPASS) {
        s_sample_finished = true;
        saadc_mgnt_disable();
        xSemaphoreGive(s_sample_mutex);
        APP_ERROR_HANDLER(ERR_BUSY);
        return ERR_FAIL;
    }
    xSemaphoreGive(s_sample_mutex);
    return ERR_NONE;
} 


SYSTEM_ERROR_CODE_E stop_bat_sample(void) {
    if((NULL == s_sample_timer) || (NULL == s_sample_mutex)) {
        return ERR_NOT_INIT;
    }
    if(xSemaphoreTake(s_sample_mutex, portMAX_DELAY) != pdTRUE) {
        return ERR_FAIL;
    }
    if(s_sample_finished) {
        xSemaphoreGive(s_sample_mutex);
        return ERR_FAIL;
    }
    s_sample_finished = true;
    power_calc_mgnt_reset();
    if(xTimerStop(s_sample_timer, 0) != pdPASS) {
        saadc_mgnt_disable();
        xSemaphoreGive(s_sample_mutex);
        APP_ERROR_HANDLER(ERR_BUSY);
        return ERR_FAIL;
    }
    saadc_mgnt_disable();
    xSemaphoreGive(s_sample_mutex);
    return ERR_NONE;
}


void bat_task(void * argument) {
    os_task_info_t *_task_info = (os_task_info_t *)argument;
    s_bat_os = &_task_info[OS_TASK_BAT];
    s_sample_mutex = xSemaphoreCreateMutex();
    ASSERT(s_sample_mutex);
    /* Parameter assert */
	ASSERT(TASK_BAT_INTERVAL_MS > (TIM_BAT_SAMPLE_PERIOD_MS * get_power_cal_mgnt_record_size()));
	
    uint8_t FSM_cob;
    if(IS_INCHARGE_TRIGGER()) {
        FSM_cob = SYS_INCHARGING_REQ;
                send_fsm_critical_request(FSM_cob);
        if(IS_FULL_CHARGE_TRIGGER()) {
            FSM_cob = SYS_FULL_CHARGED_REQ;
                        send_fsm_critical_request(FSM_cob);
			s_full_charge_reported = true;
        }
    }
    
	/* Power remain calculation initialize (include SAADC) */
    saadc_mgnt_init(saadc_cfgs, 1);
	power_calc_mgnt_init();
	
	s_sample_timer = xTimerCreate("TIM.Bat",
                                     pdMS_TO_TICKS(TIM_BAT_SAMPLE_PERIOD_MS),
                                     pdTRUE,
                                     NULL,
                                     bat_sample_timeout_handler);
	/* If pxTimerBuffer was NULL then NULL is returned. */
	ASSERT(s_sample_timer);

    s_bat_os->wake_tick = xTaskGetTickCount();
	
	/* Infinite loop */
	for(;;) {
        uint32_t _sys_state = 0;
        dev_status_get(SYSTEM_STATE, &_sys_state);
        uint32_t _is_charge = false;
        dev_status_get(INCHARGING_STATE, &_is_charge);
		full_charged_poll();
        
        /* Keep the battery ADC disabled while business sleep is active. */
        if(_sys_state != SLEEPING) {
            start_bat_sample();
        }
        
        vTaskDelayUntil(&s_bat_os->wake_tick, pdMS_TO_TICKS(TASK_BAT_INTERVAL_MS));
	}
}


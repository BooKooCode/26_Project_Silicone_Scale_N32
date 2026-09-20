#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_error.h"
#include "os_mgnt.h"
#include "button.h"
#include "dev_buzzer.h"
#include "dev_status.h"
#include "dev_config.h"
#include "timing.h"
#include "scale_digits_display.h"
#include "log.h"
#include "hci.h"

#define HCI_LOG_INFO(...)  LOG_INFO("hci", __VA_ARGS__)
#define HCI_LOG_ERROR(...) LOG_ERROR("hci", __VA_ARGS__)

typedef enum {
    HCI_REMOTE_INVALID_CMD = 0,
    HCI_REMOTE_BUZZER_MELODY,
    HCI_REMOTE_BUZZER_GEAR,
    
    HCI_REMOTE_CMD_TOTAL_COUNT
} HCI_REMOTE_CMD_TYPE_E;


typedef struct {
    uint16_t cmd_type;
    uint16_t cmd;
} hci_remote_cmd_t;


typedef struct {
    GPIO_Module *port;
    uint16_t pin;
    bool trigger_level;
    bool enabled;
} btn_gpio_hardware_t;


os_task_info_t *s_hci_os = NULL;
os_task_info_t *s_fsm_os = NULL;


static const btn_gpio_hardware_t s_btn_gpio[BUTTON_TOTAL_COUNT] = {
    {btn_left_GPIO_Port, btn_left_Pin, false, true},
    {btn_right_GPIO_Port, btn_right_Pin, true, true}
};


static uint8_t get_hci_btn_level(uint8_t index) {
    if(index >= BUTTON_TOTAL_COUNT) {
        return 0;
    }
    if(!s_btn_gpio[index].enabled || s_btn_gpio[index].port == NULL) {
        return 0;
    }
    return (GPIO_ReadInputDataBit(s_btn_gpio[index].port, s_btn_gpio[index].pin) != 0U);
}


static btn_event_mgnt_t s_btn_mgnt = {
    .btn_count = BUTTON_TOTAL_COUNT,
    .btn_event_handler = NULL,
    .btn_statis = NULL,
    .get_btn_level_handler = get_hci_btn_level
};


static int hci_left_button_response(void *param) {
    uint32_t event = (uint32_t)param;
    uint8_t FSM_cob = 0;
    uint8_t timing_cob;
    
    uint32_t btn_lock = false;
    dev_status_get(BUTTON_LOCK_STATE, &btn_lock);
    if(true == btn_lock) {
        return ERR_NONE;
    }
    
    uint32_t _sys_state = 0;
    dev_status_get(SYSTEM_STATE, &_sys_state);
    
    bool _touch_guard = false;
    get_kv_param_value(PARAM_KEY_TOUCH_GUARD, &_touch_guard);
    
    switch (event) {
        case BUTTON_DOUBLE_CLICK: {
            HCI_LOG_INFO("Left button Double clicked.");
            break;
        }
        case BUTTON_SINGLE_CLICK: {
            if(_sys_state == WORKING_CALIBRATE) {
                FSM_cob = SYS_CALI_EXITING_REQ;
                send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            else if(_sys_state == AUTO_MODE_GOING) {
                FSM_cob = SYS_TOAUTOEND_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            //FIXED:Add left click switch status under Autoend            
            else if(_sys_state == AUTO_MODE_END) {
                FSM_cob = SYS_TOAUTOREADY_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            if(_sys_state == TIMING_MODE) {
                send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                switch(timing_mgnt_getstate()) {
                    case TIMING_RESETED:{
                        timing_cob = TIMING_START_REQ;
                        send_queue_belong_task(OS_TASK_TIMREQ, &timing_cob);
                    } break;
                    case TIMING_STOPPED: {
                        timing_cob = TIMING_RESET_REQ;
                        send_queue_belong_task(OS_TASK_TIMREQ, &timing_cob);
                    } break;
                    case TIMING_GOING: {
                        timing_cob = TIMING_STOP_REQ;
                        send_queue_belong_task(OS_TASK_TIMREQ, &timing_cob);
                    } break; 
                    default: break;
                };
            }
            HCI_LOG_INFO("Left button clicked.");
            break;
        }
        case BUTTON_SHORT_PRESS: {
            HCI_LOG_INFO("Left button short_press.");
            break;
        }
        case BUTTON_MIDDLE_PRESS: {
            uint32_t _startup_ts = 0;
            dev_status_get(STARTUP_TIMESTAMP, &_startup_ts);
            if(_sys_state != SLEEPING && (xTaskGetTickCount() - _startup_ts) > 3000) {
                if(false == _touch_guard) {
                    FSM_cob = SYS_SLEEPING_REQ;
                    send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);    
                }
            }
            break;
        }
        case BUTTON_LONG_PRESS: {
            HCI_LOG_INFO("Left button long press.");
            break;
        }
        case BUTTON_SHORT_PRESS_RELEASE: {
            HCI_LOG_INFO("Left button short press-release in time.");
            break;
        }
        case BUTTON_MIDDLE_PRESS_RELEASE:
        case BUTTON_LONG_PRESS_RELEASE: {
            uint32_t _startup_ts = 0;
            dev_status_get(STARTUP_TIMESTAMP, &_startup_ts);
            if(_sys_state != SLEEPING && (xTaskGetTickCount() - _startup_ts) > 3000) {
                if(_touch_guard) {
                    FSM_cob = SYS_SLEEPING_REQ;
                    send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);    
                }
            }
            break;
        }
        case BUTTON_TRIGGER: {
            if(_sys_state >= WAKEUP) {
                /* Trigger light up led */
                digits_disp_req(DISP_LEFTPRESS_REQ);
            }
            else if(_sys_state == SLEEPING) {
                eTaskState _fsm_task_state = eTaskGetState(s_fsm_os->handler);
                if(_fsm_task_state == eSuspended) {
                    reset_button_startup_state(&s_btn_mgnt, LEFT_BUTTON_INDEX);
                    HCI_LOG_INFO("Resume left button startup!!!");
                }
            }
            break;
        }
        case BUTTON_RELEASE: {
            if(_sys_state >= WAKEUP) {
                /* Release turn off led */
                digits_disp_req(DISP_LEFTRELEASE_REQ);
            }
            break;
        }
        case BUTTON_STARTUP_SHORT_PRESS: {
            if(_sys_state == SLEEPING) {
                if(digits_disp_stateget() == DISP_RECOVERABLE_ERROR) {
                    FSM_cob = SYS_SLEEP_PROBE_RETRY_REQ;
                }
                else {
                    FSM_cob = SYS_WAKEUP_REQ;
                    send_hci_buzzer_melody(HCI_BUZZER_STARTUP_REPONSE);
                }
                (void)send_fsm_critical_request(FSM_cob);
            }
            HCI_LOG_INFO("Left button startup short press.");
            break;
        }
        case BUTTON_STARTUP_LONG_PRESS: {
            HCI_LOG_INFO("Left button startup long press.");
            break;
        }
        case (BUTTON_MULTI_CLICK + 4): {
            if(_sys_state == WEIGHTING_MODE || _sys_state == TIMING_MODE || _sys_state == AUTO_MODE_GOING || _sys_state == AUTO_MODE_END) {
                FSM_cob = SYS_SILENCE_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
                HCI_LOG_INFO("Silent switch.");
            }
            break;    
        }
        case (BUTTON_MULTI_CLICK + 6): {
            if(_sys_state == WEIGHTING_MODE || _sys_state == TIMING_MODE || _sys_state == AUTO_MODE_GOING || _sys_state == AUTO_MODE_END) {
                FSM_cob = SYS_UNIT_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
                HCI_LOG_INFO("Unit switch.");
            }
            break;
        }
        default:
            return ERR_OUT_OF_RANGE;
    }
    return ERR_NONE;
}


static int hci_right_button_response(void *param) {
    uint32_t event = (uint32_t)param;
    uint8_t FSM_cob = 0;
    
    uint32_t btn_lock = false;
    dev_status_get(BUTTON_LOCK_STATE, &btn_lock);
    if(true == btn_lock) {
        return ERR_NONE;
    }
    
    uint32_t _sys_state = 0;
    dev_status_get(SYSTEM_STATE, &_sys_state);
    
    bool _touch_guard = false;
    get_kv_param_value(PARAM_KEY_TOUCH_GUARD, &_touch_guard);
    
    switch (event) {
        case BUTTON_SINGLE_CLICK: {
            /* Peeling in weighing and espresso manual modes */
            if((_sys_state == WEIGHTING_MODE) || (_sys_state == TIMING_MODE)) {
                FSM_cob = SYS_PEELING_REQ;
                send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            /* Next AUTO_MODE_READY */
            else if(_sys_state == AUTO_MODE_END) {
                FSM_cob = SYS_TOAUTOREADY_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            HCI_LOG_INFO("Right button clicked.");
            break;
        }
        case BUTTON_DOUBLE_CLICK: {
            // /* Toggle espresso submode (manual <-> auto) in espresso modes */
            // if((_sys_state == TIMING_MODE) || (_sys_state == AUTO_MODE_READY)) {
            //     send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
            //     FSM_cob = SYS_TOGGLE_ESPRESSO_SUBMODE_REQ;
            //     send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            // }
            // /* Force reset at AUTO_MODE_GOING */
            // else if(_sys_state == AUTO_MODE_GOING) {
            //     FSM_cob = SYS_TOAUTOREADY_REQ;
            //     send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            // }
            // /* Next AUTO_MODE_READY */
            // else if(_sys_state == AUTO_MODE_END) {
            //     FSM_cob = SYS_TOAUTOREADY_REQ;
            //     send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            // }
            // send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
            HCI_LOG_INFO("Right button double clicked.");
            break;
        }
        case BUTTON_SHORT_PRESS: {
            /* Toggle between weighing and espresso mode */
            if((_sys_state >= WEIGHTING_MODE) && (_sys_state <= AUTO_MODE_READY)) {
                if(false == _touch_guard) {
                    send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                    FSM_cob = SYS_TOGGLE_ESPRESSO_REQ;
                    send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
                }
            }
            HCI_LOG_INFO("Right button short press.");
            break;
        }
        case BUTTON_MIDDLE_PRESS: {
            uint32_t _startup_ts = 0;
            dev_status_get(STARTUP_TIMESTAMP, &_startup_ts);
            if(_sys_state != SLEEPING && (xTaskGetTickCount() - _startup_ts) < 2400) {
                FSM_cob = SYS_TESTLED_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            break;
        }
        case BUTTON_LONG_PRESS: {
            HCI_LOG_INFO("Right button long press.");
            break;
        }
        case BUTTON_SHORT_PRESS_RELEASE:
        case BUTTON_MIDDLE_PRESS_RELEASE:
        case BUTTON_LONG_PRESS_RELEASE: {
            /* Toggle between weighing and espresso mode (touch_guard) */
            if((_sys_state >= WEIGHTING_MODE) && (_sys_state <= AUTO_MODE_READY)) {
                if(_touch_guard) {
                    send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                    FSM_cob = SYS_TOGGLE_ESPRESSO_REQ;
                    send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
                }
            }
            HCI_LOG_INFO("Right button short press-release in time.");
            break;
        }
        case BUTTON_TRIGGER: {
            if(_sys_state >= WAKEUP) {
                /* Trigger light up led */
                digits_disp_req(DISP_RIGHTPRESS_REQ);
            }
            break;
        }
        case BUTTON_RELEASE: {
            if(_sys_state >= WAKEUP) {
                /* Release turn off led */
                digits_disp_req(DISP_RIGHTRELEASE_REQ);
            }
            break;
        }
        case BUTTON_STARTUP_SHORT_PRESS: {
            HCI_LOG_INFO("Right button startup short press.");
            break;
        }
        case BUTTON_STARTUP_LONG_PRESS: {
            HCI_LOG_INFO("Right button startup long press.");
            break;
        }
        case (BUTTON_MULTI_CLICK + 3): {
            /* Toggle espresso submode (manual <-> auto) in espresso modes */
            if((_sys_state == TIMING_MODE) || (_sys_state == AUTO_MODE_READY)) {
                send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                FSM_cob = SYS_TOGGLE_ESPRESSO_SUBMODE_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            /* Force reset at AUTO_MODE_GOING */
                else if(_sys_state == AUTO_MODE_GOING) {
                FSM_cob = SYS_TOAUTOREADY_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            /* Next AUTO_MODE_READY */
            else if(_sys_state == AUTO_MODE_END) {
                FSM_cob = SYS_TOAUTOREADY_REQ;
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
            break;
        }
        case (BUTTON_MULTI_CLICK + 8): {
            if(_sys_state == WEIGHTING_MODE) {
                FSM_cob = SYS_CALI_REQ;
                send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
            }
            HCI_LOG_INFO("Calibration trigger.");
            break;
        }
        default:
            return ERR_OUT_OF_RANGE;
    }
    return ERR_NONE;
}


static void btn_gpio_init(void) {
    for(uint8_t i = 0; i < BUTTON_TOTAL_COUNT; i++) {
        if(!s_btn_gpio[i].enabled || s_btn_gpio[i].port == NULL) {
            continue;
        }
    }
}


static void buzzer_evt_handler(uint32_t _evt) {
	return;
}


static void buzzer_init(void) {
    dev_buzzer_init(buzzer_evt_handler);
    bool _is_sound_enable = false;
    get_kv_param_value(PARAM_KEY_SOUND_ENABLE, &_is_sound_enable);
    if(_is_sound_enable) {
        uint8_t _sound_gear = 0;
        get_kv_param_value(PARAM_KEY_SOUND_GEAR, &_sound_gear);
        dev_buzzer_changelevel((_sound_gear * 100) / BUZZER_GEAR_HIGH);
    } else {
        dev_buzzer_changelevel(0);
    }
}


SYSTEM_ERROR_CODE_E send_hci_buzzer_melody(uint16_t melody) {
    if(NULL == s_hci_os->task_queue) {
        return ERR_NOT_INIT;
    }
    if(melody >= HCI_BUZZER_MELODY_TOTAL_COUNT) {
        return ERR_INVALID_ARG;
    }
    hci_remote_cmd_t _cmd = {
        .cmd_type = HCI_REMOTE_BUZZER_MELODY,
        .cmd = melody
    };
    if(xQueueSend(s_hci_os->task_queue, &_cmd, 0) != pdTRUE) {
		APP_WARNING_HANDLER(BOOKOO_WARNING_RTOS_QUEUE_FULL);
        return ERR_FAIL;
	}
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E send_hci_buzzer_gear(uint16_t gear) {
    if(NULL == s_hci_os->task_queue) {
        return ERR_NOT_INIT;
    }
    if(gear > 5) {
        return ERR_INVALID_ARG;
    }
    hci_remote_cmd_t _cmd = {
        .cmd_type = HCI_REMOTE_BUZZER_GEAR,
        .cmd = gear
    };
    if(xQueueSend(s_hci_os->task_queue, &_cmd, 0) != pdTRUE) {
		APP_WARNING_HANDLER(BOOKOO_WARNING_RTOS_QUEUE_FULL);
        return ERR_FAIL;
	}
    return ERR_NONE;
}


void hci_task(void *argument) {
    os_task_info_t *_task_info = (os_task_info_t *)argument;
    s_fsm_os = &_task_info[OS_TASK_FSM];
    s_hci_os = &_task_info[OS_TASK_HCI];
    s_hci_os->task_queue = xQueueCreate(4, sizeof(hci_remote_cmd_t));
    ASSERT(s_hci_os->task_queue != NULL);
    
    /* Buzzer initialize */
    buzzer_init();
    
    /* Button hardware init */
    btn_gpio_init();
    
    /* Button event init */
    SYSTEM_ERROR_CODE_E ret = button_service_init(&s_btn_mgnt);
    if(ERR_NONE == ret) {
        HCI_LOG_INFO("Button middleware init success.");
    } else {
        HCI_LOG_ERROR("Button middleware init fail, error code: %d", ret);
    }
    /* Register button event */
    register_multi_click_event(&s_btn_mgnt, LEFT_BUTTON_INDEX, 4, false);
    register_multi_click_event(&s_btn_mgnt, LEFT_BUTTON_INDEX, 6, false);
    register_multi_click_event(&s_btn_mgnt, LEFT_BUTTON_INDEX, 8, false);
    register_multi_click_event(&s_btn_mgnt, RIGHT_BUTTON_INDEX, 8, true);
    register_multi_click_event(&s_btn_mgnt, RIGHT_BUTTON_INDEX, 3, false);
    register_button_response_event(&s_btn_mgnt, LEFT_BUTTON_INDEX, hci_left_button_response);
    register_button_response_event(&s_btn_mgnt, RIGHT_BUTTON_INDEX, hci_right_button_response);
    s_hci_os->wake_tick = xTaskGetTickCount();
    
    while(true) { 
        vTaskDelayUntil(&s_hci_os->wake_tick, pdMS_TO_TICKS(HCI_UPDATE_PER_MS));
        
        /* Button fsm update */
        for(uint8_t i = 0; i < BUTTON_TOTAL_COUNT; i++) {
            button_state_update(&s_btn_mgnt, i, s_btn_gpio[i].trigger_level);
        }
        
        /* Remote command update : Unblock */
        hci_remote_cmd_t _remote_cmd = {0};
        while(uxQueueMessagesWaiting(s_hci_os->task_queue)) {
            if(xQueueReceive(s_hci_os->task_queue, &_remote_cmd, 0) != pdPASS) {
                break;
            }
            if(HCI_REMOTE_BUZZER_GEAR == _remote_cmd.cmd_type) {
                dev_buzzer_changelevel((_remote_cmd.cmd * 100) / BUZZER_GEAR_HIGH);
            }
            /* Buzzer update */
            if(HCI_REMOTE_BUZZER_MELODY == _remote_cmd.cmd_type) {
                dev_buzzer_action((buzzer_songidx_s)_remote_cmd.cmd);
            }
        }
    }
}



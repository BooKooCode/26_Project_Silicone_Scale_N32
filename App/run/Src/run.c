#include "internal.h"
#include "math.h"
#include "os_mgnt.h"
#include "dev_status.h"
#include "dev_config.h"
#include "log.h"
#include "timing.h"
#include "scale_digits_display.h"

#include "bat_meas.h"
#include "hci.h"
#include "fsm.h"
#include "mass_meas.h"
#include "sleep_probe.h"


#define FSM_LOG_INFO(...)     LOG_INFO("fsm", __VA_ARGS__)



static void run_clock_request(void) {
}


static void run_clock_release(void) {
}


//typedef struct {
    /* Action counting variables for auto mode */
//    bool auto_cup_placed;
//    bool auto_firststop_trigged;
//    bool auto_mode_endreq;
    /* Lastest results for auto mode */
//    float auto_mass;
//    float auto_flowrate;
//    uint32_t auto_timing_ms;
    
//    uint8_t auto_ready_peelingwait_cnt;
//    uint32_t auto_mode_switchcnt;
//    float last_peelingmass;
//    uint32_t disp_test_count;
//    uint32_t standby_count;
    
//    bool is_incharge;
//    float gram_after_peel;
//    float flowrate_raw;
//} fsm_param_t;


// static 
fsm_mgnt_t s_fsm_mgnt;
//static fsm_param_t s_fsm_param;
static uint32_t s_espresso_submode = TIMING_MODE;  /* Remembered espresso submode for toggle */
static os_task_info_t *s_bat_os = NULL;
static os_task_info_t *s_mass_os = NULL;
static os_task_info_t *s_disp_os = NULL;
static os_task_info_t *s_fsm_os = NULL;
static os_task_info_t *s_fsmreq_os = NULL;
static bool s_cup_wakeup = false;
static bool s_cup_peeling_started = false;
static bool s_probe_error_visible = false;
static bool s_explicit_wake_startup = true;


static uint32_t sleeping_state_action(void);
static uint32_t wakeup_state_action(void);
static uint32_t wakeup_cali_state_action(void);
static void wakup_cali_state_exit(uint32_t state);
static uint32_t weighting_mode_action(void);
static void weighting_mode_enter(void);
static uint32_t timming_mode_action(void);
static void timing_mode_enter(void);
static uint32_t auto_mode_ready_action(void);
static void auto_mode_ready_enter(void);
static uint32_t auto_mode_going_action(void);
static uint32_t auto_mode_end_action(void);
static uint32_t calibrating_state_action(void);
static void calibrating_state_enter(void);
static uint32_t lowpower_protect_action(void);
static uint32_t display_testing_action(void);
static void display_testing_enter(void);

const fsm_iterate_entry_t s_fsm_item[] = {
    /* 
    |       state code      |           action          |           enter           |           exit            |
    */
    {   SLEEPING,               sleeping_state_action,      NULL,                       NULL                    },
    {   WAKEUP,                 wakeup_state_action,        NULL,                       NULL                    },
    {   WAKEUP_CALIBRATE,       wakeup_cali_state_action,   NULL,                       wakup_cali_state_exit   },
    {   WEIGHTING_MODE,         weighting_mode_action,      weighting_mode_enter,       NULL                    },
    {   TIMING_MODE,            timming_mode_action,        timing_mode_enter,          NULL                    },
    {   RESERVED_MODE_5,        NULL,                       NULL,                       NULL                    },
    {   AUTO_MODE_READY,        auto_mode_ready_action,     auto_mode_ready_enter,      NULL                    },
    {   AUTO_MODE_GOING,        auto_mode_going_action,     NULL,                       NULL                    },
    {   AUTO_MODE_END,          auto_mode_end_action,       NULL,                       NULL                    },
    {   WORKING_CALIBRATE,      calibrating_state_action,   calibrating_state_enter,    NULL                    },
    {   SYS_LOWPOWER_PROTECT,   lowpower_protect_action,    NULL,                       NULL                    },
    {   DISP_TEST_MODE,         display_testing_action,     display_testing_enter,      NULL                    },
};

static void fsm_var_set_bool(VARS_CACHE_INDEX_E idx, bool val) {
    FORMAT_4BYTES_U var_value = { 0 };
    var_value.us8[0] = val ? 1U : 0U;
    dev_var_set(idx, &var_value);
}


static bool fsm_var_get_bool(VARS_CACHE_INDEX_E idx) {
    FORMAT_4BYTES_U var_value = { 0 };
    dev_var_get(idx, &var_value);
    return (var_value.us8[0] != 0U);
}


static void set_run_state(uint32_t state) {
    dev_status_set(SYSTEM_STATE, state);
}


static uint32_t get_run_state(void) {
    uint32_t _state = 0;
    dev_status_get(SYSTEM_STATE, &_state);
    return _state;
}

static bool is_charging_active(void)
{
    uint32_t charging = false;

    dev_status_get(INCHARGING_STATE, &charging);
    return charging ||
            (GPIO_ReadInputDataBit(CHARGING_ENABLE_GPIO_Port, CHARGING_ENABLE_Pin) == 0U);
}


static bool need_standby(void) {
    /* Standby for user_config.waiting_max_minute (15min by default) */
//    if(scale_mass_mgnt_stableget()) {
    FORMAT_4BYTES_U standby_count = { 0 };
    dev_var_get(FSM_STANDBY_COUNT, (FORMAT_4BYTES_U *)&standby_count);
    
    if(is_mass_standby_stable()) {        
        standby_count.us32++;
        
        float _standby_min = 0.0f;
        get_kv_param_value(PARAM_KEY_STANDBY_MIN, &_standby_min);
        if(standby_count.us32 > ((uint32_t)(_standby_min * 60000.0f) / TASK_FSM_INTERVAL_MS)) {
            /* Transfer the system state */
            dev_var_set(FSM_STANDBY_COUNT, (FORMAT_4BYTES_U *)&standby_count);
            FSM_LOG_INFO("Standby timeout! Going to SLEEPING");
            return true;
        }
    }
    else {
        if(standby_count.us32 > 100) {
            standby_count.us32 -= 20;
        }
        else {
            standby_count.us32 = 0;
        }
    }
    dev_var_set(FSM_STANDBY_COUNT, (FORMAT_4BYTES_U *)&standby_count);
    return false;
}


//static void fsm_param_prepare(void) {
//    uint32_t _is_incharging = false;
//    dev_status_get(INCHARGING_STATE, &_is_incharging);
//    
//    s_fsm_param.is_incharge = _is_incharging;
//   
//    FORMAT_4BYTES_U _mass_after_peel;
//    dev_var_get(MASS_AFTER_PEEL_INDEX, &_mass_after_peel);
//    s_fsm_param.gram_after_peel = _mass_after_peel.fl32;
//    
//    FORMAT_4BYTES_U _flowrate_raw;
//    dev_var_get(FLOWRATE_RAW_INDEX, (FORMAT_4BYTES_U *)&_flowrate_raw);
//    s_fsm_param.flowrate_raw = _flowrate_raw.fl32;
//}


static void sync_automode_var(void) {
    FORMAT_4BYTES_U auto_mass = { 0 };
    FORMAT_4BYTES_U auto_flowrate = { 0 };
    FORMAT_4BYTES_U auto_timing_ms = { 0 };
    dev_var_get(FSM_AUTO_MASS, (FORMAT_4BYTES_U *)&auto_mass);
    dev_var_get(FSM_AUTO_FLOWRATE, (FORMAT_4BYTES_U *)&auto_flowrate);
    dev_var_get(FSM_AUTO_TIMING_MS, (FORMAT_4BYTES_U *)&auto_timing_ms);
    
    dev_var_set(AUTO_MASS_INDEX, (FORMAT_4BYTES_U *)&auto_mass);
    dev_var_set(AUTO_FLOWRATE_INDEX, (FORMAT_4BYTES_U *)&auto_flowrate);
    dev_var_set(AUTO_TIMING_MS_INDEX, (FORMAT_4BYTES_U *)&auto_timing_ms);
}


static uint32_t sleeping_state_action(void) {
    
    fsm_var_set_bool(FSM_AUTO_CUP_PLACED, false);
    fsm_var_set_bool(FSM_AUTO_FIRSTSTOP_TRIGGED, false);
    fsm_var_set_bool(FSM_AUTO_MODE_ENDREQ, false);
    
    FORMAT_4BYTES_U auto_mass = { 0 };
    FORMAT_4BYTES_U auto_flowrate = { 0 };
    FORMAT_4BYTES_U standby_count = { 0 };
    FORMAT_4BYTES_U auto_mode_switchcnt = { 0 };
    FORMAT_4BYTES_U auto_timing_ms = { 0 };
    FORMAT_4BYTES_U auto_ready_peelingwait_cnt = { 0 };
    auto_ready_peelingwait_cnt.us8[0] = AUTO_MODE_PEELING_COUNT;
    
    dev_var_set(FSM_AUTO_MASS, (FORMAT_4BYTES_U *)&auto_mass);
    dev_var_set(FSM_AUTO_FLOWRATE, (FORMAT_4BYTES_U *)&auto_flowrate);
    dev_var_set(FSM_STANDBY_COUNT, (FORMAT_4BYTES_U *)&standby_count);
    dev_var_set(FSM_AUTO_MODE_SWITCHCNT, (FORMAT_4BYTES_U *)&auto_mode_switchcnt);
    dev_var_set(FSM_AUTO_TIMING_MS, (FORMAT_4BYTES_U *)&auto_timing_ms);
    dev_var_set(FSM_AUTO_READY_PEELINGWAIT_CNT, (FORMAT_4BYTES_U *)&auto_ready_peelingwait_cnt);
//    s_fsm_param.auto_mass = 0.0f;
//    s_fsm_param.auto_flowrate = 0.0f;
//    s_fsm_param.auto_timing_ms = 0;
//    s_fsm_param.auto_cup_placed = false;
//    s_fsm_param.auto_firststop_trigged = false;
//    s_fsm_param.auto_ready_peelingwait_cnt = AUTO_MODE_PEELING_COUNT;
//    s_fsm_param.standby_count = 0;
    /* Reset the action counting variables */
//    s_fsm_param.auto_mode_switchcnt = 0;
//    s_fsm_param.auto_mode_endreq = false;
    
    timing_mgnt_req(TIMING_FORCE_RESET_REQ);
    uint32_t _is_incharging = false;
    dev_status_get(INCHARGING_STATE, &_is_incharging);

    if(false == _is_incharging) {
        uint8_t stale_request;
        while(xQueueReceive(s_fsm_os->task_queue, &stale_request, 0) == pdPASS) {
        }
        digits_disp_req(DISP_HALT_REQ);
    }
    run_clock_release();
    stop_bat_sample();

    while(false == _is_incharging) {
        uint8_t _req_cob;
        if(xQueueReceive(s_fsm_os->task_queue, &_req_cob,
                         pdMS_TO_TICKS(TASK_FSM_INTERVAL_MS)) != pdPASS) {
            if(get_run_state() != SLEEPING) {
                return get_run_state();
            }
            continue;
        }
        if(_req_cob == SLEEPING) {
            SYSTEM_ERROR_CODE_E _store_res = ERR_NONE;
            for(uint32_t _attempt = 0; _attempt < 2U; _attempt++) {
                _store_res = need_write_config();
                if(ERR_NONE == _store_res) {
                    _store_res = wait_config_store();
                }
                if(ERR_NONE == _store_res) {
                    break;
                }
                FSM_LOG_INFO("Config store before sleep failed: %d", _store_res);
            }
            break;
        }
    }

    dev_status_get(INCHARGING_STATE, &_is_incharging);
    if(GPIO_ReadInputDataBit(CHARGING_ENABLE_GPIO_Port, CHARGING_ENABLE_Pin) == 0U) {
        _is_incharging = true;
    }
#if SLEEP_PROBE_CUP_WAKE_ENABLED
    mass_sleep_probe_start();
    mass_sleep_probe_pause(_is_incharging);
#else
    if(!_is_incharging) {
        dev_hw_shutdown();
    }
#endif
    
    vTaskSuspend(NULL);
    s_fsm_os->wake_tick = xTaskGetTickCount();
    
    return SLEEPING;
}


static uint32_t wakeup_state_action(void) {
    /* Hardware Power on */
    dev_hw_poweron();
    run_clock_request();
    if(s_explicit_wake_startup) {
        mass_explicit_wake_startup();
        vTaskResume(s_mass_os->handler);
    }
    else {
        mass_sleep_probe_stop();
    }
    dev_status_set(BUTTON_LOCK_STATE, false);
    /* Resume the display manager task */
    digits_disp_req(DISP_STARTUP_REQ);
    digits_disp_req(DISP_INTOBUSY_REQ);
    vTaskResume(s_disp_os->handler);
    /* Resume the battery manager task */
//    vTaskResume(s_bat_os->handler);
    FSM_LOG_INFO("Going to WAKEUP_CALIBRATE");
    
    return WAKEUP_CALIBRATE;
}


static uint32_t wakeup_cali_state_action(void) {
    if(s_cup_wakeup && !mass_sleep_probe_normal_ready()) {
        return WAKEUP_CALIBRATE;
    }
    if(s_cup_wakeup && !s_cup_peeling_started &&
       (scale_mass_mgnt_stateget() == MASS_ACQ)) {
        digits_disp_req(DISP_INTOBUSY_REQ);
        scale_mass_mgnt_req(PEELING_REQ);
        s_cup_peeling_started = true;
    }
    if(s_cup_wakeup && s_cup_peeling_started &&
       (scale_mass_mgnt_stateget() != MASS_ACQ)) {
        if(digits_disp_stateget() == DISP_FINISHSTARTUP) {
            digits_disp_req(DISP_STARTUP_REQ);
        }
        return WAKEUP_CALIBRATE;
    }
    /* Mass manager startup complete */
    if((scale_mass_mgnt_stateget() != MASS_ACQ) || (digits_disp_stateget() != DISP_FINISHSTARTUP)) {
        return WAKEUP_CALIBRATE;
    }

    /* Get startup system state */
    uint8_t persisted_mode = WEIGHTING_MODE;
    if(get_kv_param_value(PARAM_KEY_INIT_MODE, &persisted_mode) != ERR_NONE) {
        persisted_mode = WEIGHTING_MODE;
    }
    
    if(persisted_mode < WEIGHTING_MODE || persisted_mode > AUTO_MODE_READY || persisted_mode == RESERVED_MODE_5) {
        persisted_mode = WEIGHTING_MODE;
        if(ERR_NONE == set_kv_param_value(PARAM_KEY_INIT_MODE, &persisted_mode)) {
            need_write_config();
        }
    }
    uint32_t state = persisted_mode;
    /* Initialize espresso submode memory from saved state */
    if(state == TIMING_MODE || state == AUTO_MODE_READY) {
        s_espresso_submode = state;
    }
    digits_disp_req(DISP_EXITBUSY_REQ);
    s_cup_wakeup = false;
    s_cup_peeling_started = false;
    return state;
}


static void wakup_cali_state_exit(uint32_t state) {
    /* Voice enable display */
    uint8_t is_voice_enable = 0;
    get_kv_param_value(PARAM_KEY_SOUND_ENABLE, &is_voice_enable);
    if(is_voice_enable) {
        digits_disp_req(DISP_VOICE_ENABLE_REQ); 
    }
    else {
        digits_disp_req(DISP_VOICE_DISABLE_REQ);
    }
    /* Mass unit display */
    uint8_t mass_unit= 0;
    get_kv_param_value(PARAM_KEY_MASS_UNIT, &mass_unit);
    if(mass_unit == MASS_UNIT_OZ) {
        digits_disp_req(DISP_OZ_MODE_REQ); 
    }
    else if(mass_unit == MASS_UNIT_G) {
        digits_disp_req(DISP_G_MODE_REQ);
    }
    FSM_LOG_INFO("Startup going to system mode: %d", state);
}


static uint32_t weighting_mode_action(void) {
    if(need_standby()) {
        return SLEEPING;
    }
    return WEIGHTING_MODE;
}


static void weighting_mode_enter(void) {
    digits_disp_req(DISP_WEIGHTING_REQ);
    FSM_LOG_INFO("SYS_SWITCHING_REQ! Going to WEIGHTING_MODE");
}


static uint32_t timming_mode_action(void) {
    if(need_standby()) {
        return SLEEPING;
    }
    return TIMING_MODE;
}


static void timing_mode_enter(void) {
    uint8_t timing_req = TIMING_FORCE_RESET_REQ;
    send_queue_belong_task(OS_TASK_TIMREQ, &timing_req);
    digits_disp_req(DISP_TIMING_REQ);
    FSM_LOG_INFO("SYS_SWITCHING_REQ! Going to TIMING_MODE");
}


static uint32_t auto_mode_ready_action(void) {
    if(need_standby()) {
        return SLEEPING;
    }
    /* Mask auto trigger right after mode switch to skip button press disturbance */
    FORMAT_4BYTES_U auto_trig_mask_ts = { 0 };
    dev_var_get(FSM_AUTO_TRIG_MASK_TS, &auto_trig_mask_ts);
    if(auto_trig_mask_ts.us32 != 0U) {
        uint32_t now_tick = xTaskGetTickCount();
        if((now_tick - auto_trig_mask_ts.us32) < pdMS_TO_TICKS(MASS_MEAS_RAW_DIFF_BUFF_MS)) {
            return AUTO_MODE_READY;
        }
    }

    FORMAT_4BYTES_U mass_after_peel = { 0 };
    FORMAT_4BYTES_U flowrate_raw = { 0 };
    FORMAT_4BYTES_U auto_ready_peelingwait_cnt = { 0 };
    dev_var_get(MASS_AFTER_PEEL_INDEX, &mass_after_peel);
    dev_var_get(FLOWRATE_RAW_INDEX, &flowrate_raw);
    dev_var_get(FSM_AUTO_READY_PEELINGWAIT_CNT, &auto_ready_peelingwait_cnt);

    /* Auto peeling */
    if((auto_ready_peelingwait_cnt.us8[0] != 0U) && scale_mass_mgnt_stableget() &&
       (mass_after_peel.fl32 != 0.0f)) {
        auto_ready_peelingwait_cnt.us8[0]--;
    }
    else {
        auto_ready_peelingwait_cnt.us8[0] = AUTO_MODE_PEELING_COUNT;
    }

    if(scale_mass_mgnt_stableget() && (fabsf(mass_after_peel.fl32) > 0.1f) &&
       (auto_ready_peelingwait_cnt.us8[0] == 0U) &&
       (scale_mass_mgnt_stateget() == MASS_ACQ)) {
        send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
        scale_mass_mgnt_req(PEELING_REQ);
        auto_ready_peelingwait_cnt.us8[0] = AUTO_MODE_PEELING_COUNT;
        dev_var_set(FSM_LAST_PEELINGMASS, &mass_after_peel);
    }

    FORMAT_4BYTES_U auto_mode_switchcnt = { 0 };
    FORMAT_4BYTES_U last_peelingmass = { 0 };
    dev_var_get(FSM_AUTO_MODE_SWITCHCNT, &auto_mode_switchcnt);
    dev_var_get(FSM_LAST_PEELINGMASS, &last_peelingmass);
    /* Cup has been placed */
    if((flowrate_raw.fl32 > 0.2f) && (flowrate_raw.fl32 < 40.0f)) {
        auto_mode_switchcnt.us32++;
        if(auto_mode_switchcnt.us32 >= 5U) {
            auto_mode_switchcnt.us32 = 0U;
            dev_var_set(FSM_AUTO_MODE_SWITCHCNT, &auto_mode_switchcnt);
            if((scale_mass_mgnt_stateget() == PEELING) && (fabsf(last_peelingmass.fl32) < 1.0f)) {
                scale_mass_mgnt_req(PEELING_EXIT_REQ);
            }
            last_peelingmass.fl32 = 0.0f;
            auto_ready_peelingwait_cnt.us8[0] = AUTO_MODE_PEELING_COUNT;
            dev_var_set(FSM_AUTO_READY_PEELINGWAIT_CNT, &auto_ready_peelingwait_cnt);
            dev_var_set(FSM_LAST_PEELINGMASS, &last_peelingmass);
            send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
            digits_disp_req(DISP_AUTOGOING_REQ);
            FSM_LOG_INFO("Extraction start! going to AUTO_MODE_GOING");
            timing_mgnt_set_initcount(1000);
            uint8_t timing_req = TIMING_START_REQ;
            send_queue_belong_task(OS_TASK_TIMREQ, &timing_req);
            return AUTO_MODE_GOING;
        }
    }
    else {
        auto_mode_switchcnt.us32 = 0U;
    }

    dev_var_set(FSM_AUTO_MODE_SWITCHCNT, &auto_mode_switchcnt);
    dev_var_set(FSM_AUTO_READY_PEELINGWAIT_CNT, &auto_ready_peelingwait_cnt);
    return AUTO_MODE_READY;
}


static void auto_mode_ready_enter(void) {
    uint8_t timing_req = TIMING_FORCE_RESET_REQ;
    FORMAT_4BYTES_U auto_ready_peelingwait_cnt = { 0 };
    auto_ready_peelingwait_cnt.us8[0] = AUTO_MODE_PEELING_COUNT;
    dev_var_set(FSM_AUTO_READY_PEELINGWAIT_CNT, &auto_ready_peelingwait_cnt);
    send_queue_belong_task(OS_TASK_TIMREQ, &timing_req);
    digits_disp_req(DISP_AUTOREADY_REQ);
    digits_disp_autowelcome(true);
    scale_mass_mgnt_req(DIRECTLY_PEELING_REQ);
    FSM_LOG_INFO("SYS_SWITCHING_REQ! Going to DISP_AUTOREADY_REQ");
}


static uint32_t auto_mode_going_action(void) {
    if(need_standby()) {
        return SLEEPING;
    }

    /* Transmit to AUTO_MODE_END */
    bool auto_mode_endreq = false;
    auto_mode_endreq = fsm_var_get_bool(FSM_AUTO_MODE_ENDREQ);
    if(auto_mode_endreq) {
        send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);

        fsm_var_set_bool(FSM_AUTO_CUP_PLACED, false);
        fsm_var_set_bool(FSM_AUTO_MODE_ENDREQ, false);
//        s_fsm_param.auto_mode_endreq = false;
//        s_fsm_param.auto_cup_placed = false;
        /* Timing stop */
        uint8_t timing_req = TIMING_FORCE_RESET_REQ;
        send_queue_belong_task(OS_TASK_TIMREQ, &timing_req);
        /* Directly peeling, reset the mass to zero */
        scale_mass_mgnt_req(PEELING_REQ);
        digits_disp_req(DISP_AUTOEND_REQ);
        digits_disp_autowelcome(false);
        FSM_LOG_INFO("Extraction end! going to AUTO_MODE_END");
        return AUTO_MODE_END;
    }
    
    AUTO_MODE_STOP_METHOD_E _stop_method;
    get_kv_param_value(PARAM_KEY_AUTO_STOP, &_stop_method);
    
    FORMAT_4BYTES_U _mass_after_peel = { 0 };
    dev_var_get(MASS_AFTER_PEEL_INDEX, &_mass_after_peel); 
    FORMAT_4BYTES_U _flowrate_raw = { 0 };
    dev_var_get(FLOWRATE_RAW_INDEX, (FORMAT_4BYTES_U *)&_flowrate_raw);
    FORMAT_4BYTES_U auto_mode_switchcnt = { 0 };
    dev_var_get(FSM_AUTO_MODE_SWITCHCNT, (FORMAT_4BYTES_U *)&auto_mode_switchcnt);
    
    
    /* The cup with coffee has been removed */
    if(_stop_method == MOVE_STOP) {
        if((_mass_after_peel.fl32 < (-AUTO_MODE_CUP_LOWEST_GRAM + 0.1f)) && (scale_mass_mgnt_stableget())) {
            auto_mode_switchcnt.us32++;
            if(auto_mode_switchcnt.us32 > 5) {
                auto_mode_switchcnt.us32 = 0;
                //s_fsm_param.auto_mode_endreq = true;
                fsm_var_set_bool(FSM_AUTO_MODE_ENDREQ, true);
            }
        }
        else {
            auto_mode_switchcnt.us32 = 0;
        }
    }
    else if(_stop_method == FLOW_STOP) {
        /* Added minimum flow-rate judgment 20251125 */
        if((_flowrate_raw.fl32 < 0.05f) && (_mass_after_peel.fl32 >= AUTO_MODE_STOP_LOWEST_GRAM) && scale_mass_mgnt_stableget()) {
            auto_mode_switchcnt.us32++;
            if(auto_mode_switchcnt.us32 > 5U) {
                auto_mode_switchcnt.us32 = 0U;
                //s_fsm_param.auto_mode_endreq = true;
                fsm_var_set_bool(FSM_AUTO_MODE_ENDREQ, true);
            }
        }
        else {
            auto_mode_switchcnt.us32 = 0;
        }
    }
    dev_var_set(FSM_AUTO_MODE_SWITCHCNT, (FORMAT_4BYTES_U *)&auto_mode_switchcnt);
    
    /* Track the extraction mass */
    /* Added minimum flow-rate judgment 20251125 */
    if((_flowrate_raw.fl32 < 0.05f) && (_mass_after_peel.fl32 >= 0.0f) && scale_mass_mgnt_stableget()) {
        bool auto_firststop_trigged = false;
        FORMAT_4BYTES_U auto_timing_ms = { 0 };
        FORMAT_4BYTES_U auto_mass = { 0 };
        FORMAT_4BYTES_U auto_flowrate = { 0 };
        
        auto_firststop_trigged = fsm_var_get_bool(FSM_AUTO_FIRSTSTOP_TRIGGED);   
        if(false == auto_firststop_trigged) {
            auto_timing_ms.us32 = timing_mgnt_get_pcount();
            auto_mass.fl32 = _mass_after_peel.fl32;
            
            dev_var_set(FSM_AUTO_MASS, (FORMAT_4BYTES_U *)&auto_mass);
            dev_var_set(FSM_AUTO_TIMING_MS, (FORMAT_4BYTES_U *)&auto_timing_ms);            
//            s_fsm_param.auto_mass = _mass_after_peel.fl32;
//            s_fsm_param.auto_timing_ms = timing_mgnt_get_pcount();
            if(auto_timing_ms.us32 > 0) {
                /* Only meaningful when the time duration is higher than 0 */               
                dev_var_get(FSM_AUTO_MASS, (FORMAT_4BYTES_U *)&auto_mass);                
                auto_flowrate.fl32 = auto_mass.fl32 / (float)(auto_timing_ms.us32) * 1000.0f;
                auto_flowrate.fl32 = roundf(auto_flowrate.fl32 * 10.0f) / 10.0f;
                dev_var_set(FSM_AUTO_FLOWRATE, (FORMAT_4BYTES_U *)&auto_flowrate);
            }
            else {
                /* Otherwise, keep the lastest valid result */
            }
            fsm_var_set_bool(FSM_AUTO_FIRSTSTOP_TRIGGED, true);
            //s_fsm_param.auto_firststop_trigged = true;
        }
    }
    else {
        fsm_var_set_bool(FSM_AUTO_FIRSTSTOP_TRIGGED, false);
        //s_fsm_param.auto_firststop_trigged = false;
    }
    sync_automode_var();
    return AUTO_MODE_GOING;
}


static uint32_t auto_mode_end_action(void) {
    if(need_standby()) {
        return SLEEPING;
    }
    return AUTO_MODE_END;
}

static uint32_t calibrating_state_action(void) {
    switch(scale_mass_mgnt_stateget()) {
        case CALI_K_WAITING: {
            if(digits_disp_stateget() != DISP_CALIK_WAITING_ANIDONE) {
                digits_disp_req(DISP_CALIK_WAITING_REQ);
            }
        } break;
        
        case CALI_K_READYPULL: {
            if(digits_disp_stateget() == DISP_CALIK_WAITING_ANIDONE) {
                digits_disp_req(DISP_CALIK_PULL_REQ);
            }
        } break;
            
        case CALI_K_PULLWAITING: {
            digits_disp_req(DISP_CALIK_PULL_REQ);
        } break;
        
        case CALI_K_SUCCESS: {
            digits_disp_req(DISP_CALIK_SUCCESS_REQ);
        } break;
        
        case MASS_ACQ: {
            /* Calibration result storage */
            float _k = scale_mass_mgnt_caliKget();
            SYSTEM_ERROR_CODE_E _store_res = set_kv_param_value(PARAM_KEY_MASS_CALI_K, &_k);
            if(ERR_NONE == _store_res) {
                _store_res = need_write_config();
            }
            if(ERR_NONE != _store_res) {
                FSM_LOG_INFO("Calibration config queue failed: %d", _store_res);
            }
            /* Transfer the system state */
            FORMAT_4BYTES_U standby_count = { 0 };
            dev_var_set(FSM_STANDBY_COUNT, (FORMAT_4BYTES_U *)&standby_count);
//            s_fsm_param.standby_count = 0;
            digits_disp_req(DISP_WEIGHTING_REQ);
            digits_disp_req(DISP_EXITBUSY_REQ);
            FSM_LOG_INFO("Calibreation done, going to OFFLINE_MODE");
            uint32_t _requested_state = get_run_state();
            if(_requested_state != WORKING_CALIBRATE) {
                return _requested_state;
            }
            return WEIGHTING_MODE;
        } break;
        
        default: 
            break;
    } // END SWITCH
    return WORKING_CALIBRATE;
}


static void calibrating_state_enter(void) {
    digits_disp_req(DISP_INTOBUSY_REQ);
    scale_mass_mgnt_req(CALI_K_REQ);
    FSM_LOG_INFO("Calibration request received! Going to WORKING_CALIBRATE");
}


static uint32_t lowpower_protect_action(void) {
    if(digits_disp_stateget() == DISP_LOWPOWER_PROTECT_WAITING) {
        return SLEEPING;
    }
    return SYS_LOWPOWER_PROTECT;
}


static uint32_t display_testing_action(void) {
    FORMAT_4BYTES_U disp_test_count = { 0 };
    dev_var_get(FSM_DISP_TEST_COUNT, (FORMAT_4BYTES_U *)&disp_test_count);
    disp_test_count.us32 ++;
    if(disp_test_count.us32 > (8000 / TASK_FSM_INTERVAL_MS)) {
        disp_test_count.us32 = 0;
        dev_var_set(FSM_DISP_TEST_COUNT, (FORMAT_4BYTES_U *)&disp_test_count);
        FSM_LOG_INFO("Test mode finished! Going to SLEEPING");
        return SLEEPING;
    }
    dev_var_set(FSM_DISP_TEST_COUNT, (FORMAT_4BYTES_U *)&disp_test_count);
    return DISP_TEST_MODE;
}


static void display_testing_enter(void) {
    dev_status_set(BUTTON_LOCK_STATE, true);
    digits_disp_req(DISP_TESTLED_REQ);
    FSM_LOG_INFO("Display test.");
}


void FSM_task(void * argument) {
    os_task_info_t *_task_info = (os_task_info_t *)argument;
    s_fsm_os = &_task_info[OS_TASK_FSM];
    s_bat_os = &_task_info[OS_TASK_BAT];
    s_mass_os = &_task_info[OS_TASK_MASS];
    s_disp_os = &_task_info[OS_TASK_DISP];
    s_fsmreq_os = &_task_info[OS_TASK_FSMREQ];
    
    s_fsm_os->task_queue = xQueueCreate(4, sizeof(uint8_t));
    ASSERT(s_fsm_os->task_queue != NULL);
	/* Pre-Load for task */
	s_fsm_os->wake_tick = xTaskGetTickCount();
//	memset(&s_fsm_param, 0, sizeof(fsm_param_t));
    /* Init fsm mgnt */
    SYSTEM_ERROR_CODE_E _ret = fsm_init(&s_fsm_mgnt, s_fsm_item, SYSTEM_STAT_TOTAL_COUNT, set_run_state, get_run_state);
    ASSERT(_ret == ERR_NONE);
    ASSERT(s_fsmreq_os->handler);
    xTaskNotifyGive(s_fsmreq_os->handler);
    
    /* Halting the task at the first time since the low-power consumption request */
	vTaskSuspend(NULL);
    s_fsm_os->wake_tick = xTaskGetTickCount();
	/* Infinite loop */
	for(;;) {
		vTaskDelayUntil(&s_fsm_os->wake_tick, pdMS_TO_TICKS(TASK_FSM_INTERVAL_MS));
        /* Prepare parameters which fsm used */
//        fsm_param_prepare();
        fsm_iterate_run(&s_fsm_mgnt);
	}	// END TASK LOOP
}


void FSMreq_task(void * argument) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    os_task_info_t *_task_info = (os_task_info_t *)argument;
    s_fsmreq_os = &_task_info[OS_TASK_FSMREQ];
    s_fsmreq_os->task_queue = xQueueCreate(4, sizeof(uint8_t));
	ASSERT(s_fsmreq_os->task_queue != NULL);
	/* Pre-Load for task */
	uint8_t FSMreq_cob;
	s_fsmreq_os->wake_tick = xTaskGetTickCount();
	/* Infinite loop */
	for(;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        do {
		/* If there exists any FSM modify request */
        if(!take_fsm_critical_request(&FSMreq_cob) &&
           (xQueueReceive(s_fsmreq_os->task_queue, &FSMreq_cob, 0) != pdPASS)) {
            break;
        }
        uint32_t _sys_state = get_run_state();
        /* Prepare parameters which fsm used */
//        fsm_param_prepare();
        
        switch(FSMreq_cob) {
            case SYS_SLEEPING_REQ: {
                dev_status_set(BUTTON_LOCK_STATE, true);
                set_run_state(SLEEPING);
                FSM_LOG_INFO("Sleeping request received! Going to SLEEPING");
            } break;
            
            case SYS_WAKEUP_REQ: {
                if(is_charging_active()) {
                    break;
                }
                s_cup_wakeup = false;
                s_cup_peeling_started = false;
                s_probe_error_visible = false;
                s_explicit_wake_startup = true;
                set_run_state(WAKEUP);
                vTaskResume(s_fsm_os->handler);
                /* Set latest startup timestatmp */
                dev_status_set(STARTUP_TIMESTAMP, xTaskGetTickCount());
                FSM_LOG_INFO("Wakeup request received! Going to WAKEUP, mass task=%u disp task=%u", 
                             (unsigned int)eTaskGetState(s_mass_os->handler),
                             (unsigned int)eTaskGetState(s_disp_os->handler));
            } break;

            case SYS_CUP_WAKEUP_REQ: {
                if((_sys_state == SLEEPING) && !is_charging_active() && !s_probe_error_visible) {
                    s_cup_wakeup = true;
                    s_cup_peeling_started = false;
                    s_explicit_wake_startup = false;
                    set_run_state(WAKEUP);
                    vTaskResume(s_fsm_os->handler);
                    dev_status_set(STARTUP_TIMESTAMP, xTaskGetTickCount());
                    FSM_LOG_INFO("Cup wakeup request received");
                }
            } break;

            case SYS_SLEEP_PROBE_ERROR_REQ: {
                if((_sys_state == SLEEPING) && !is_charging_active() && !s_probe_error_visible) {
                    s_probe_error_visible = true;
                    vTaskResume(s_disp_os->handler);
                    digits_disp_recoverable_error(BOOKOO_ERROR_MASS_LOWPOWER_PROBE_FAIL);
                    FSM_LOG_INFO("Sleep probe error");
                }
            } break;

            case SYS_SLEEP_PROBE_RECOVERED_REQ: {
                if((_sys_state == SLEEPING) && !is_charging_active() && s_probe_error_visible) {
                    s_probe_error_visible = false;
                    s_cup_wakeup = false;
                    s_explicit_wake_startup = true;
                    set_run_state(WAKEUP);
                    vTaskResume(s_fsm_os->handler);
                    dev_status_set(STARTUP_TIMESTAMP, xTaskGetTickCount());
                    FSM_LOG_INFO("Sleep probe recovered");
                }
            } break;

            case SYS_SLEEP_PROBE_RETRY_REQ: {
                if((_sys_state == SLEEPING) && s_probe_error_visible) {
                    mass_sleep_probe_pause(false);
                }
            } break;
            
            case SYS_CALI_REQ: {
                set_run_state(WORKING_CALIBRATE);
            } break;
            
            case SYS_PEELING_REQ: {
                digits_disp_req(DISP_INTOBUSY_REQ);
                digits_disp_req(DISP_PEELING_REQ);
                scale_mass_mgnt_req(PEELING_REQ);
                FSM_LOG_INFO("Peeling request");
            } break;
            
            case SYS_NEXT_MODE_REQ: {
                /* Since the longpress will be recongnized as a peeling command first, a peeling exit request is needed */
                scale_mass_mgnt_req(PEELING_EXIT_REQ);
                uint8_t _req_state = 0;
                uint8_t _mask = 0;
                get_kv_param_value(PARAM_KEY_MODE_MASK, &_mask);
                uint32_t _cur_state_code = (_sys_state - WEIGHTING_MODE);
                /* Find next active mode */
                for(uint8_t i = 0; i < 4; i++) {
                    _cur_state_code ++;
                    if(_cur_state_code >= 4) {
                        _cur_state_code -= 4;
                    }
                    if((WEIGHTING_MODE + _cur_state_code) == RESERVED_MODE_5) {
                        continue;
                    }
                    if(_mask & (1 << _cur_state_code)) {
                        _req_state = WEIGHTING_MODE + _cur_state_code;
                        break;
                    }
                }
                     if((_req_state >= WEIGHTING_MODE) && (_req_state <= AUTO_MODE_READY) &&
                         (_req_state != RESERVED_MODE_5)) {
                    set_run_state(_req_state);
                    /* Store mode change */
                    if(ERR_NONE == set_kv_param_value(PARAM_KEY_INIT_MODE, &_req_state)) {
                        need_write_config();
                    }
                }
            } break;
            
            case SYS_CALI_EXITING_REQ: {
                if(scale_mass_mgnt_stateget() != CALI_K_SUCCESS) {
                    scale_mass_mgnt_req(CALI_K_EXIT_REQ);
                    digits_disp_req(DISP_WEIGHTING_REQ);
                    set_run_state(WEIGHTING_MODE);
                    FSM_LOG_INFO("Calibration teminated! Going to WEIGHTING_MODE");
                }
            } break;
                
            case SYS_LOWPOWER_PROTECT_REQ: {
                if(_sys_state != SLEEPING) {
                    dev_status_set(BUTTON_LOCK_STATE, true);
                    digits_disp_req(DISP_LOWPOWER_PROTECT_REQ);
                    set_run_state(SYS_LOWPOWER_PROTECT);
                    FSM_LOG_INFO("Low power protection! Going to SYS_LOWPOWER_PROTECT");
                }
            } break;
            
            case SYS_INCHARGING_REQ: {
//                vTaskResume(s_bat_os->handler);
                dev_status_set(INCHARGING_STATE, true);
                s_cup_wakeup = false;
                s_probe_error_visible = false;
                mass_sleep_probe_pause(true);
                if(digits_disp_stateget() != DISP_INCHARGING) {
                    dev_status_set(BUTTON_LOCK_STATE, true);
                    if((_sys_state == SLEEPING) && (digits_disp_stateget() == DISP_HALT)) {
                        send_hci_buzzer_melody(HCI_BUZZER_INCHARGE_RESPONSE);
                        vTaskResume(s_disp_os->handler);
                    }
                    else {
                        set_run_state(SLEEPING);
                    }
                    FSM_LOG_INFO("In charging");
                    digits_disp_req(DISP_INCHARGING_REQ);
                }
            } break;

            case SYS_TESTLED_REQ: {
                if(digits_disp_stateget() != DISP_TESTLED) {
                    if((_sys_state == SLEEPING) && (digits_disp_stateget() == DISP_HALT)) {
                        vTaskResume(s_disp_os->handler);
                    }
                    set_run_state(DISP_TEST_MODE);
                }
            } break;
            
            case SYS_SILENCE_REQ: {
                bool _is_sound_enable = false;
                get_kv_param_value(PARAM_KEY_SOUND_ENABLE, &_is_sound_enable);
                if(_is_sound_enable) {
                    digits_disp_req(DISP_VOICE_DISABLE_REQ);
                    _is_sound_enable = false;
                    send_hci_buzzer_gear(0);
                } 
                else {
                    digits_disp_req(DISP_VOICE_ENABLE_REQ);
                    _is_sound_enable = true;
                    uint8_t _sound_gear = 0;
                    get_kv_param_value(PARAM_KEY_SOUND_GEAR, &_sound_gear);
                    send_hci_buzzer_gear(_sound_gear);
                    send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                }
                if(ERR_NONE == set_kv_param_value(PARAM_KEY_SOUND_ENABLE, &_is_sound_enable)) {
                    need_write_config();
                }
            } break;
            
            case SYS_UNIT_REQ: {
                uint8_t _mass_unit = MASS_UNIT_G;
                get_kv_param_value(PARAM_KEY_MASS_UNIT, &_mass_unit);
                if(MASS_UNIT_G == _mass_unit) {
                    digits_disp_req(DISP_OZ_MODE_REQ);
                    _mass_unit = MASS_UNIT_OZ;
                }
                else {
                    digits_disp_req(DISP_G_MODE_REQ);
                    _mass_unit = MASS_UNIT_G;
                }
                if(ERR_NONE == set_kv_param_value(PARAM_KEY_MASS_UNIT, &_mass_unit)) {
                    need_write_config();
                }
                send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
            } break;
            
            case SYS_EXITCHARGING_REQ: {
                dev_status_set(INCHARGING_STATE, false);
                if(digits_disp_stateget() == DISP_INCHARGING) {
                    digits_disp_req(DISP_EXITCHARGING_REQ);
                    /* Battery sampling is best effort and must not block charge-exit UI. */
                    if(ERR_NONE == start_bat_sample()) {
                        FSM_LOG_INFO("Battery sampling enable");
                    }
                }
                if(_sys_state == SLEEPING) {
                    mass_sleep_probe_reset_zero_baseline();
                    mass_sleep_probe_pause(false);
                }
            } break;
            
            case SYS_FULL_CHARGED_REQ: {
                if(digits_disp_stateget() == DISP_INCHARGING) {
                    digits_disp_req(DISP_INFULLCHARGING_REQ);
                    FSM_LOG_INFO("Battery Full Charge");
                }
            } break;
            
            case SYS_TOAUTOREADY_REQ: {
                /* Directly peeling, reset the mass to zero */
                send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                scale_mass_mgnt_req(PEELING_REQ);

                FORMAT_4BYTES_U auto_trig_mask_ts = { 0 };
                auto_trig_mask_ts.us32 = xTaskGetTickCount();
                dev_var_set(FSM_AUTO_TRIG_MASK_TS, &auto_trig_mask_ts);

                FORMAT_4BYTES_U auto_mode_switchcnt = { 0 };
                dev_var_set(FSM_AUTO_MODE_SWITCHCNT, &auto_mode_switchcnt);

                if((_sys_state == AUTO_MODE_GOING) || (_sys_state == AUTO_MODE_END)) {
                    FORMAT_4BYTES_U auto_mass = { 0 };
                    FORMAT_4BYTES_U auto_flowrate = { 0 };
                    FORMAT_4BYTES_U auto_timing_ms = { 0 };
                    dev_var_set(FSM_AUTO_MASS, &auto_mass);
                    dev_var_set(FSM_AUTO_FLOWRATE, &auto_flowrate);
                    dev_var_set(FSM_AUTO_TIMING_MS, &auto_timing_ms);
                    set_run_state(AUTO_MODE_READY);
                }
            } break;
            
            case SYS_TOAUTOGOING_REQ: {
                send_hci_buzzer_melody(HCI_BUZZER_CLICK_RESPONSE);
                set_run_state(AUTO_MODE_GOING);
                digits_disp_req(DISP_AUTOGOING_REQ);
                FSM_LOG_INFO("Extraction start from the APP! going to AUTO_MODE_GOING");
                timing_mgnt_set_initcount(1000);
                uint8_t timing_req = TIMING_START_REQ;
                send_queue_belong_task(OS_TASK_TIMREQ, &timing_req);
            } break;
            
            case SYS_TOAUTOEND_REQ: {
                FORMAT_4BYTES_U timing_ms = { 0 };
                FORMAT_4BYTES_U auto_mass = { 0 };
                FORMAT_4BYTES_U auto_flow = { 0 };

                scale_mass_mgnt_history_mass_get(&auto_mass.fl32);
                timing_ms.us32 = timing_mgnt_get_pcount();
                if(timing_ms.us32 > 0U) {
                    auto_flow.fl32 = auto_mass.fl32 / (float)timing_ms.us32 * 1000.0f;
                    auto_flow.fl32 = roundf(auto_flow.fl32 * 10.0f) / 10.0f;
                }
                else {
                    dev_var_get(FSM_AUTO_FLOWRATE, &auto_flow);
                }
                dev_var_set(FSM_AUTO_FLOWRATE, &auto_flow);
                dev_var_set(FSM_AUTO_MASS, &auto_mass);
                dev_var_set(FSM_AUTO_TIMING_MS, &timing_ms);
                fsm_var_set_bool(FSM_AUTO_MODE_ENDREQ, true);
                sync_automode_var();
            } break;
            
            case SYS_TOAUTOEND_APP_REQ: {
                FORMAT_4BYTES_U mass_after_peel = {0};
                FORMAT_4BYTES_U auto_mass = {0};
                FORMAT_4BYTES_U timing_ms = {0};
                FORMAT_4BYTES_U auto_flow = {0};

                dev_var_get(MASS_AFTER_PEEL_INDEX, &mass_after_peel);
                auto_mass.fl32 = mass_after_peel.fl32;
                timing_ms.us32 = timing_mgnt_get_pcount();

                if (timing_ms.us32 > 0) {
                    auto_flow.fl32 = auto_mass.fl32 / (float)timing_ms.us32 * 1000.0f;
                    auto_flow.fl32 = roundf(auto_flow.fl32 * 10.0f) / 10.0f;
                } else {
                    dev_var_get(FSM_AUTO_FLOWRATE, &auto_flow);
                }

                dev_var_set(FSM_AUTO_MASS, &auto_mass);
                dev_var_set(FSM_AUTO_TIMING_MS, &timing_ms);
                dev_var_set(FSM_AUTO_FLOWRATE, &auto_flow);
                fsm_var_set_bool(FSM_AUTO_MODE_ENDREQ, true);

                sync_automode_var();
            } break;
            
            case SYS_TOGGLE_ESPRESSO_REQ: {
                scale_mass_mgnt_req(PEELING_EXIT_REQ);
                uint8_t _persisted_mode;
                if(_sys_state == WEIGHTING_MODE) {
                    /* Switch from weighing to espresso: restore memorised espresso submode */
                    _persisted_mode = s_espresso_submode;
                    set_run_state(_persisted_mode);
                } else {
                    /* Switch from espresso to weighing */
                    s_espresso_submode = _sys_state;
                    _persisted_mode = WEIGHTING_MODE;
                    set_run_state(WEIGHTING_MODE);
                }
                /* Always persist current mode for boot */
                if(ERR_NONE == set_kv_param_value(PARAM_KEY_INIT_MODE, &_persisted_mode)) {
                    need_write_config();
                }
            } break;
            
            case SYS_TOGGLE_ESPRESSO_SUBMODE_REQ: {
                scale_mass_mgnt_req(PEELING_EXIT_REQ);
                uint8_t _new_mode = 0;
                if(_sys_state == TIMING_MODE) {
                    _new_mode = AUTO_MODE_READY;
                } else if(_sys_state == AUTO_MODE_READY) {
                    _new_mode = TIMING_MODE;
                } else {
                    break;
                }
                s_espresso_submode = _new_mode;
                set_run_state(_new_mode);
                if(ERR_NONE == set_kv_param_value(PARAM_KEY_INIT_MODE, &_new_mode)) {
                    need_write_config();
                }
            } break;
            
        }	// END SWITCH
        
        /* Proactively blocking to execute other tasks */
        s_fsmreq_os->wake_tick = xTaskGetTickCount();
        vTaskDelayUntil(&s_fsmreq_os->wake_tick, pdMS_TO_TICKS(TASK_FSM_REQ_INTERVAL_MIN_MS));
		} while(true);
	}
}


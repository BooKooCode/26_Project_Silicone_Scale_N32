#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "os_mgnt.h"
#include "dev_status.h"
#include "dev_config.h"
#include "dev_buzzer.h"
#include "log.h"
#include "regular.h"
#include "sleep_probe.h"
#include "SEGGER_RTT.h"
#include "scale_mass_mgnt.h"
#include "util.h"


#define REGULAR_LOG_INFO(...)     LOG_INFO("regular", __VA_ARGS__)
#define REGULAR_LOG_ERROR(...)    LOG_ERROR("regular", __VA_ARGS__)


__WEAK void app_watchdog_feed(void) {
}


#define RTT_CMD_BEGIN           "sc"

#define RTT_CMD_RESET           "reset"
#define RTT_CMD_STORE           "store"
#define RTT_CMD_RESTORE         "restore"
#define RTT_CMD_ERASE_ALL       "erase_all"
#define RTT_CMD_WAKEUP          "wakeup"
#define RTT_CMD_SLEEP           "sleep"
#define RTT_CMD_RUN_MODE        "run_mode"
#define RTT_CMD_MODE_MASK       "mode_mask"
#define RTT_CMD_WRITE_PARAM     "w_param"
#define RTT_CMD_READ_PARAM      "r_param"
#define RTT_CMD_DELETE_PARAM    "d_param"
#define RTT_CMD_FILTER          "filter"

#define RTT_FILTER_GET          "get"
#define RTT_FILTER_SET          "set"

#define RTT_CMD_GET_INFO        "get_info"

#define RTT_INFO_VERSION        "version"
#define RTT_INFO_COMMIT_TS      "commit_ts"
#define RTT_INFO_COMPILE_TS     "compile_ts"
#define RTT_INFO_AUTHOR         "author"
#define RTT_INFO_CHIP_UID       "chip_uid"
#define RTT_INFO_EXT_UID        "ext_uid"
#define RTT_INFO_WRITE_NB       "ext_write_nb"
#define RTT_INFO_DEV_STATE      "dev_state"


static void* s_calloc(size_t num, size_t size) {
#if USE_RTOS_MEMORY
    void *addr = pvPortMalloc(num * size);
    memset(addr, 0, num * size);
    return addr;
#else
    return calloc(num, size);
#endif
}


static void s_free(void *addr) {
#if USE_RTOS_MEMORY
    vPortFree(addr);
#else
    free(addr);
#endif
}


static void get_target_info(char *target) {
    if(NULL == target) {
        REGULAR_LOG_ERROR("Invalid info request");
        return;
    }
    if(0 == strncmp(target, RTT_INFO_VERSION, strlen(RTT_INFO_VERSION))) {
        char *_version_str = NULL;
        dev_var_get(COMPILE_VERSION_INDEX, (FORMAT_4BYTES_U *)&_version_str);
        REGULAR_LOG_INFO("[%s] %s : %s", RTT_CMD_GET_INFO, RTT_INFO_VERSION, _version_str);
    }
    else if(0 == strncmp(target, RTT_INFO_COMMIT_TS, strlen(RTT_INFO_COMMIT_TS))) {
        char *_compile_str = NULL;
        dev_var_get(LATEST_COMMIT_TS_INDEX, (FORMAT_4BYTES_U *)&_compile_str);
        REGULAR_LOG_INFO("[%s] %s : %s", RTT_CMD_GET_INFO, RTT_INFO_COMMIT_TS, _compile_str);
    }
    else if(0 == strncmp(target, RTT_INFO_COMPILE_TS, strlen(RTT_INFO_COMPILE_TS))) {
        char *_commit_str = NULL;
        dev_var_get(LATEST_COMPILE_TS_INDEX, (FORMAT_4BYTES_U *)&_commit_str);
        REGULAR_LOG_INFO("[%s] %s : %s", RTT_CMD_GET_INFO, RTT_INFO_COMPILE_TS, _commit_str);
    }
    else if(0 == strncmp(target, RTT_INFO_AUTHOR, strlen(RTT_INFO_AUTHOR))) {
        char *_author_str = NULL;
        dev_var_get(LATEST_AUTHOR_INDEX, (FORMAT_4BYTES_U *)&_author_str);
        REGULAR_LOG_INFO("[%s] %s : %s", RTT_CMD_GET_INFO, RTT_INFO_AUTHOR, _author_str);
    }
    else if(0 == strncmp(target, RTT_INFO_CHIP_UID, strlen(RTT_INFO_CHIP_UID))) {
        FORMAT_8BYTES_U _uid = {0};
        dev_var_get(CHIP_UID_LO_INDEX, (FORMAT_4BYTES_U *)&_uid.us32[0]);
        dev_var_get(CHIP_UID_HI_INDEX, (FORMAT_4BYTES_U *)&_uid.us32[1]);
        REGULAR_LOG_INFO("[%s] %s : %08x%08x", RTT_CMD_GET_INFO, RTT_INFO_CHIP_UID, _uid.us32[1], _uid.us32[0]);
    }
    else if(0 == strncmp(target, RTT_INFO_EXT_UID, strlen(RTT_INFO_EXT_UID))) {
        FORMAT_8BYTES_U _uid = {0};
        dev_var_get(EXT_FLASH_UID_LO_INDEX, (FORMAT_4BYTES_U *)&_uid.us32[0]);
        dev_var_get(EXT_FLASH_UID_HI_INDEX, (FORMAT_4BYTES_U *)&_uid.us32[1]);
        REGULAR_LOG_INFO("[%s] %s : %08x%08x", RTT_CMD_GET_INFO, RTT_INFO_EXT_UID, _uid.us32[1], _uid.us32[0]);
    }
    else if(0 == strncmp(target, RTT_INFO_WRITE_NB, strlen(RTT_INFO_WRITE_NB))) {
        uint32_t _write_count = 0;
        dev_var_get(EXT_FLASH_WRITE_COUNT_INDEX, (FORMAT_4BYTES_U *)&_write_count);
        REGULAR_LOG_INFO("[%s] %s : %d", RTT_CMD_GET_INFO, RTT_INFO_WRITE_NB, _write_count);
    }
    else if(0 == strncmp(target, RTT_INFO_DEV_STATE, strlen(RTT_INFO_DEV_STATE))) {
        uint32_t _state = 0;
        dev_status_get(SYSTEM_STATE, &_state);
        REGULAR_LOG_INFO("[%s] %s : %d", RTT_CMD_GET_INFO, RTT_INFO_DEV_STATE, _state);
    }
    else {
        REGULAR_LOG_ERROR("[%s] %s : Fail - Cannot find this item!", RTT_CMD_GET_INFO, target);
    }
}


static void read_param(char *key) {
    uint8_t _type = 0;
    SYSTEM_ERROR_CODE_E _find_res = get_kv_item_type(key, &_type);
    if(_find_res != ERR_NONE) {
        REGULAR_LOG_ERROR("[%s] %s : Fail - Cannot find this key!", RTT_CMD_READ_PARAM, key);
        return;
    }
    if(STRING_TYPE == _type) {
        uint16_t _len = 0;
        get_kv_item_len(key, &_len);
        char *_str = s_calloc(1, (_len + 1));
        if(NULL == _str) {
            REGULAR_LOG_ERROR("[%s] %s : Fail - Malloc data memory fail!", RTT_CMD_READ_PARAM, key);
            return;
        }
        get_kv_str_value(key, _str, _len); 
        REGULAR_LOG_INFO("[%s] %s : %s", RTT_CMD_READ_PARAM, key, _str);
        s_free(_str);
    } 
    else {
        FORMAT_4BYTES_U _value = {0};
        get_kv_param_value(key, &_value); 
        if(FL32_TYPE == _type) {
            REGULAR_LOG_INFO("[%s] %s : " NRF_FLOAT_8VALID_MARKER, RTT_CMD_READ_PARAM, key, NRF_FLOAT_8VALID(_value.fl32));
        }
        else if(US8_TYPE == _type || US16_TYPE == _type || US32_TYPE == _type) {
            REGULAR_LOG_INFO("[%s] %s : %d", RTT_CMD_READ_PARAM, key, _value.us32);
        }
        else if(IS8_TYPE == _type || IS16_TYPE == _type || IS32_TYPE == _type) {
            REGULAR_LOG_INFO("[%s] %s : %d", RTT_CMD_READ_PARAM, key, _value.is32);
        }
    }
}


static void delete_param(char *key) {
    uint8_t _type = 0;
    /* Search for the existence of parameters */
    SYSTEM_ERROR_CODE_E _find_res = get_kv_item_type(key, &_type);
    if(_find_res != ERR_NONE) {
        REGULAR_LOG_ERROR("[%s] %s : Fail - Cannot find this key!", RTT_CMD_DELETE_PARAM, key);
        return;
    }
    SYSTEM_ERROR_CODE_E _delete_res = delete_kv_item(key);
    if(ERR_NONE != _delete_res) {
        REGULAR_LOG_ERROR("[%s] %s : Fail - Delete str fail, error code: %d", RTT_CMD_DELETE_PARAM, key, _delete_res);
        return;
    }
    REGULAR_LOG_INFO("[%s] %s : Success.", RTT_CMD_DELETE_PARAM, key);
}


static void write_param(char *key, char *value) {
    uint8_t _type = 0;
    SYSTEM_ERROR_CODE_E _find_res = get_kv_item_type(key, &_type);
    if(_find_res != ERR_NONE) {
        REGULAR_LOG_ERROR("[%s] %s : Fail - Cannot find this key!", RTT_CMD_WRITE_PARAM, key);
        return;
    }
    if(NULL == value) {
        REGULAR_LOG_ERROR("[%s] %s : Fail - Value is Null", RTT_CMD_WRITE_PARAM, key);
        return;
    }
    bool _is_number = is_valid_number(value);
    if(STRING_TYPE == _type) {
        if(_is_number) {
            REGULAR_LOG_ERROR("[%s] %s : Fail - Param type is string, but write value is number!", RTT_CMD_WRITE_PARAM, key);
            return;
        }
        uint16_t value_len = strlen(value);
        if(value_len == 0 || value_len > STORE_STRING_MAX_LEN) {
            REGULAR_LOG_ERROR("[%s] %s : Fail - String is too long!", RTT_CMD_WRITE_PARAM, key);
            return;
        }
        SYSTEM_ERROR_CODE_E _set_res = set_kv_str_value(key, value, value_len);
        if(ERR_NONE != _set_res) {
            REGULAR_LOG_ERROR("[%s] %s : Fail - Write str fail, error code: %d", RTT_CMD_WRITE_PARAM, key, _set_res);
            return;
        }
        REGULAR_LOG_INFO("[%s] %s : Success.", RTT_CMD_WRITE_PARAM, key);
    }
    else {
        if(false == _is_number) {
            REGULAR_LOG_ERROR("[%s] %s : Fail - Param type is a number, but write value is string!", RTT_CMD_WRITE_PARAM, key);
            return;
        }
        CONV_ERROR_E _conv_err = CONV_OK;
        if(FL32_TYPE == _type) {
            float _fp32 = str_to_float(value, &_conv_err);
            if(_conv_err != CONV_OK) {
                REGULAR_LOG_ERROR("[%s] %s : Fail - Invalid float format: %d", RTT_CMD_WRITE_PARAM, key, _conv_err);
                return;
            }
            SYSTEM_ERROR_CODE_E _set_res = set_kv_param_value(key, &_fp32);
            if(ERR_NONE != _set_res) {
                REGULAR_LOG_ERROR("[%s] %s : Fail - Write float fail, error code: %d", RTT_CMD_WRITE_PARAM, key, _set_res);
                return;
            }
        }
        else {
            int32_t _is32 = str_to_int32(value, &_conv_err);
            if(_conv_err != CONV_OK) {
                REGULAR_LOG_ERROR("[%s] %s : Fail - Invalid integer format: %d", RTT_CMD_WRITE_PARAM, key, _conv_err);
                return;
            }
            FORMAT_4BYTES_U numeric_value = {0};
            switch(_type) {
                case US8_TYPE: numeric_value.us8[0] = (uint8_t)_is32; break;
                case IS8_TYPE: numeric_value.is8[0] = (int8_t)_is32; break;
                case US16_TYPE: numeric_value.us16[0] = (uint16_t)_is32; break;
                case IS16_TYPE: numeric_value.is16[0] = (int16_t)_is32; break;
                case US32_TYPE: numeric_value.us32 = (uint32_t)_is32; break;
                case IS32_TYPE: numeric_value.is32 = _is32; break;
                default:
                    REGULAR_LOG_ERROR("[%s] %s : Fail - Unsupported numeric type: %u", RTT_CMD_WRITE_PARAM, key, _type);
                    return;
            }
            SYSTEM_ERROR_CODE_E _set_res = set_kv_param_value(key, &numeric_value);
            if(ERR_NONE != _set_res) {
                REGULAR_LOG_ERROR("[%s] %s : Fail - Write str fail, error code: %d", RTT_CMD_WRITE_PARAM, key, _set_res);
                return;
            }
        }
        REGULAR_LOG_INFO("[%s] %s : Success.", RTT_CMD_WRITE_PARAM, key);
    }
}


static void filter_response(const char *result, const char *reason) {
    float cutoff_hz = 0.0f;
    uint32_t window = 0U;
    scale_mass_mgnt_filter_config_get(&cutoff_hz, &window);
    int32_t cutoff_millihz = (int32_t)roundf(cutoff_hz * 1000.0f);

    if(reason == NULL) {
        SEGGER_RTT_printf(0,
                         "[filter] result=%s cutoff_hz=%d.%03d window=%u\n",
                         result,
                         cutoff_millihz / 1000,
                         cutoff_millihz % 1000,
                         window);
    }
    else {
        SEGGER_RTT_printf(0, "[filter] result=error reason=%s\n", reason);
    }
}


static void read_rtt_recv(uint32_t _sys_state) {
    int _count = 0;
    int _remain_len = 0;
    char _buf[64] = {0};
    char _remain_buf[64] = {0};
    char _cmd[MAX_PARTS][MAX_LEN];
    
    uint8_t _read_count = 0;
    while(SEGGER_RTT_HasData(0) && (_read_count < 10)) {
        if(_remain_len > 0) {
            memcpy(_buf, _remain_buf, _remain_len);
            memset(_remain_buf, 0, 64);
        }
        uint16_t _rtt_len = (int)SEGGER_RTT_Read(0, &_buf[_remain_len], (64 - _remain_len));
        uint16_t _valid_len = strlen(_buf);
        if(_rtt_len > (_valid_len + 1)) {
            _remain_len = _rtt_len - (_valid_len + 1);
            memcpy(_remain_buf, &_buf[_valid_len + 1], _remain_len);
        }
        split_string(_buf, _cmd, &_count);
        _read_count ++;
        
        if(0 != strncmp(_cmd[0], RTT_CMD_BEGIN, strlen(RTT_CMD_BEGIN))) {
            continue;
        }
        if(2 == _count) {
            if(0 == strncmp(_cmd[1], RTT_CMD_RESET, strlen(RTT_CMD_RESET))) {
                REGULAR_LOG_INFO("mcu reset...");
                /* Reset cmd */
                dev_sw_reset();
            }
            else if(0 == strncmp(_cmd[1], RTT_CMD_STORE, strlen(RTT_CMD_STORE))) {
                REGULAR_LOG_INFO("config store.");
                /* Write config */
                need_write_config();
            }
            else if(0 == strncmp(_cmd[1], RTT_CMD_RESTORE, strlen(RTT_CMD_RESTORE))) {
                REGULAR_LOG_INFO("config restore factory.");
                /* Restore cmd */
                need_restore_factory();
            }
            else if(0 == strncmp(_cmd[1], RTT_CMD_ERASE_ALL, strlen(RTT_CMD_ERASE_ALL))) {
                REGULAR_LOG_INFO("erase all ext flash data.");
                /* Erase all */
                need_erase_all();
            }
            else if(0 == strncmp(_cmd[1], RTT_CMD_WAKEUP, strlen(RTT_CMD_WAKEUP))) {
                REGULAR_LOG_INFO("Soft wakeup.");
                uint32_t _sys_state = 0;
                dev_status_get(SYSTEM_STATE, &_sys_state);
                /* Wakeup cmd */
                if(_sys_state == SLEEPING) {
                    uint8_t FSM_cob = 0;
                    FSM_cob = SYS_WAKEUP_REQ;
                    dev_buzzer_action(SONG_STARTUP);
                    send_fsm_critical_request(FSM_cob);
                }
            } 
            else if(0 == strncmp(_cmd[1], RTT_CMD_SLEEP, strlen(RTT_CMD_SLEEP))) {
                REGULAR_LOG_INFO("Soft sleep.");
                uint32_t _sys_state = 0;
                dev_status_get(SYSTEM_STATE, &_sys_state);
                /* Sleep cmd */
                if(_sys_state != SLEEPING) {
                    uint8_t FSM_cob = 0;
                    FSM_cob = SYS_SLEEPING_REQ;
                    send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
                }
            } 
            else {
                REGULAR_LOG_ERROR("invalid cmd!!!");
            }
        }
        else if(3 == _count) {
            /* The third parameter is a number */
            if(is_valid_number(_cmd[2])) {
                uint32_t param = parse_hexadecimal(_cmd[2]);
                if(0 == strncmp(_cmd[1], RTT_CMD_RUN_MODE, strlen(RTT_CMD_RUN_MODE))) {
                    /* Switch running mode */
                    if((param <= 3) && (param != (RESERVED_MODE_5 - WEIGHTING_MODE))) {
                        uint8_t _new_state = (uint8_t)(WEIGHTING_MODE + param);
                        if(ERR_NONE != set_kv_param_value(PARAM_KEY_INIT_MODE, &_new_state)) {
                            REGULAR_LOG_ERROR("Run mode set fail!");
                            continue;
                        }
                        dev_status_set(SYSTEM_STATE, _new_state);
                        need_write_config();
                        dev_buzzer_action(SONG_PRESSBTN);
                    } else {
                        REGULAR_LOG_ERROR("Invalid Mode Code: %d", param);
                    }
                }
                else if(0 == strncmp(_cmd[1], RTT_CMD_MODE_MASK, strlen(RTT_CMD_MODE_MASK))) {
                    if((param != 0U) && ((param & ~0x0BU) == 0U)) {
                        uint8_t mode_mask = (uint8_t)param;
                        if(ERR_NONE != set_kv_param_value(PARAM_KEY_MODE_MASK, &mode_mask)) {
                            REGULAR_LOG_ERROR("Mode mask set fail!");
                            continue;
                        }
                        dev_buzzer_action(SONG_PRESSBTN);
                        need_write_config();
                        /* Must change current mode */
                        if(0 == (param & (1 << (_sys_state - WEIGHTING_MODE)))) {
                            uint8_t FSM_cob = SYS_NEXT_MODE_REQ;
                            send_queue_belong_task(OS_TASK_FSMREQ, &FSM_cob);
                        }
                    } else {
                        REGULAR_LOG_ERROR("Mode mask must use bits 0, 1 and 3.");
                    }
                }
            }
            /* The third parameter is a string */
            else {
                if(0 == strncmp(_cmd[1], RTT_CMD_GET_INFO, strlen(RTT_CMD_GET_INFO))) {
                    /* Get device info */
                    get_target_info(_cmd[2]);
                }
                else if(0 == strncmp(_cmd[1], RTT_CMD_READ_PARAM, strlen(RTT_CMD_READ_PARAM))) {
                    /* Read param */
                    uint16_t key_len = strlen(_cmd[2]);
                    if(key_len == 0 || key_len > STORE_KEY_MAX_LEN) {
                        REGULAR_LOG_ERROR("The Key of param is too long.");
                        continue;
                    }
                    read_param(_cmd[2]);
                }
                else if(0 == strncmp(_cmd[1], RTT_CMD_DELETE_PARAM, strlen(RTT_CMD_DELETE_PARAM))) {
                    uint16_t key_len = strlen(_cmd[2]);
                    if(key_len == 0 || key_len > STORE_KEY_MAX_LEN) {
                        REGULAR_LOG_ERROR("The Key of param is too long.");
                        continue;
                    }
                    delete_param(_cmd[2]);
                }
                else if((0 == strcmp(_cmd[1], RTT_CMD_FILTER)) &&
                        (0 == strcmp(_cmd[2], RTT_FILTER_GET))) {
                    filter_response("ok", NULL);
                }
            }
        }   
        else if(4 == _count) {
            if(is_valid_number(_cmd[2])) {
                
            } 
            /* The third parameter is a string */
            else {
                if(0 == strncmp(_cmd[1], RTT_CMD_WRITE_PARAM, strlen(RTT_CMD_WRITE_PARAM))) {
                    uint16_t key_len = strlen(_cmd[2]);
                    if(key_len == 0 || key_len > STORE_KEY_MAX_LEN) {
                        REGULAR_LOG_ERROR("The Key of param is too long.");
                        continue;
                    }
                    write_param(_cmd[2], _cmd[3]);
                }
            }
        }
        else if(5 == _count) {
            if((0 == strcmp(_cmd[1], RTT_CMD_FILTER)) &&
               (0 == strcmp(_cmd[2], RTT_FILTER_SET))) {
                CONV_ERROR_E cutoff_error = CONV_OK;
                CONV_ERROR_E window_error = CONV_OK;
                float cutoff_hz = str_to_float(_cmd[3], &cutoff_error);
                int32_t window = str_to_int32(_cmd[4], &window_error);

                if(cutoff_error != CONV_OK || window_error != CONV_OK) {
                    filter_response("error", "invalid_number");
                }
                else if(window <= 0 ||
                        scale_mass_mgnt_filter_config_set(cutoff_hz,
                                                          (uint32_t)window) != NS_SUCCESS) {
                    filter_response("error", "out_of_range");
                }
                else {
                    filter_response("ok", NULL);
                }
            }
        }
        
    }
}


void regularity_task(void * argument) {
    os_task_info_t *_task_info = (os_task_info_t *)argument;
	/* Pre-Load for task */
	_task_info[OS_TASK_REGULAR].wake_tick = xTaskGetTickCount();

	/* Infinite loop */
	for(;;) {
        uint32_t _sys_state = 0;
        dev_status_get(SYSTEM_STATE, &_sys_state);
        uint32_t delay_ms = (_sys_state == SLEEPING) ?
                            SLEEP_PROBE_PERIOD_MS : TASK_REGULAR_WAKEUP_INTERVAL_MS;
		vTaskDelayUntil(&_task_info[OS_TASK_REGULAR].wake_tick, pdMS_TO_TICKS(delay_ms));
		dev_status_get(SYSTEM_STATE, &_sys_state);

        app_watchdog_feed();
        if(_sys_state != SLEEPING) {
            /* Scan rtos thread state */
            for(uint8_t i = OS_TASK_REGULAR; i < OS_TASK_TOTAL_COUNT; i++) {
                _task_info[i].free_stack = uxTaskGetStackHighWaterMark(_task_info[i].handler);
            }
        }        
        /* Decode rtt cmd */
        read_rtt_recv(_sys_state);
	}
}

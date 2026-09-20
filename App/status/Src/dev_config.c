#include "internal.h"
#include "dev_sfud_mgnt.h"
#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"
#include "os_mgnt.h"
#include "ext_store.h"
#include "sfud.h"
#include "dev_name.h"
#include "dev_config.h"
#include "log.h"
#include "bookoo_error_def.h"

typedef struct {
    uicr_user_data_s uicr_data;
    EventGroupHandle_t event;
    os_task_info_t *store_os;
} config_mgnt_t;


static config_mgnt_t s_cfg_mgnt;
static volatile uint32_t s_store_request_seq;
static volatile uint32_t s_store_complete_seq;
static volatile SYSTEM_ERROR_CODE_E s_store_result = ERR_NONE;


#define DEV_CONFIG_LOG_ERROR(...)   LOG_ERROR("dev_config", __VA_ARGS__)
#define DEV_CONFIG_LOG_INFO(...)    LOG_INFO("dev_config", __VA_ARGS__)


numeric_param_t numeric_params[] = {
    /* 
    |   parameter name          |   default parameter                   |       low limit           |       high limit          |       parameter type      |
    */
    {   PARAM_KEY_STANDBY_MIN,      .default_val.fl32 = DE_CFG_STANDBY_TIME,    .lo_limit.fl32 = 5.0f,      .hi_limit.fl32 = 30.0f,     .type = FL32_TYPE   },
    {   PARAM_KEY_MASS_CALI_K,      .default_val.fl32 = DE_CFG_MASS_CALI_K,     .lo_limit.fl32 = 0,         .hi_limit.fl32 = 0,         .type = FL32_TYPE   },
    {   PARAM_KEY_SENSOR_TYPE,      .default_val.us8[0] = HONGBO2KG_2MVPV,      .lo_limit.us8[0] = 0,       .hi_limit.us8[0] = 2,       .type = US8_TYPE    },
    {   PARAM_KEY_MASS_UNIT,        .default_val.us8[0] = DE_CFG_MASS_UNIT,     .lo_limit.us8[0] = 1,       .hi_limit.us8[0] = 2,       .type = US8_TYPE    },
    {   PARAM_KEY_SOUND_GEAR,       .default_val.us8[0] = DE_CFG_BUZZER_GEAR,   .lo_limit.us8[0] = 0,       .hi_limit.us8[0] = 5,       .type = US8_TYPE    },
    {   PARAM_KEY_INIT_MODE,        .default_val.us8[0] = DE_CFG_INIT_MODE,     .lo_limit.us8[0] = 3,       .hi_limit.us8[0] = 6,       .type = US8_TYPE    },
    {   PARAM_KEY_SMOOTH_ENABLE,    .default_val.us8[0] = DE_SMOOTH_ENABLE,     .lo_limit.us8[0] = false,   .hi_limit.us8[0] = true,    .type = US8_TYPE    },
    {   PARAM_KEY_AUTO_STOP,        .default_val.us8[0] = FLOW_STOP,            .lo_limit.us8[0] = 0,       .hi_limit.us8[0] = 1,       .type = US8_TYPE    },
    {   PARAM_KEY_SOUND_ENABLE,     .default_val.us8[0] = true,                 .lo_limit.us8[0] = false,   .hi_limit.us8[0] = true,    .type = US8_TYPE    },
    {   PARAM_KEY_MODE_MASK,        .default_val.us8[0] = 0x0b,                 .lo_limit.us8[0] = 0x01,    .hi_limit.us8[0] = 0x0f,    .type = US8_TYPE    },
    {   PARAM_KEY_ANI_CYCLE,        .default_val.us16[0] = DE_ANI_CYCLE_MS,     .lo_limit.us16[0] = 10,     .hi_limit.us16[0] = 1000,   .type = US16_TYPE   },
    {   PARAM_KEY_TOUCH_GUARD,      .default_val.us8[0] = false,                .lo_limit.us8[0] = 0,       .hi_limit.us8[0] = 1,       .type = US8_TYPE    }
};


string_param_t str_params[] = {
    /* 
    |   parameter name          |   default parameter                       |   string length   |
    */
    {   STRING_KEY_DEV_NAME,        .default_val = DEV_NAME_TEMPLATE,           .val_max_len = DEV_NAME_SIZE    },
    {   "reserve_str1",             .default_val = "placeholder",               .val_max_len = 24               }
};


static bool check_params_validation(numeric_param_t *param, FORMAT_4BYTES_U *value) {
    if(NULL == param || NULL == value) {
        return false;
    }
    /* If the upper and lower limits are equal, considered as not limiting the range of values */
    if(param->lo_limit.us32 == param->hi_limit.us32) {
        return true;
    }
    switch(param->type) {
        case US8_TYPE: {
            if(value->us8[0] < param->lo_limit.us8[0] || value->us8[0] > param->hi_limit.us8[0]) {
                return false;
            }
        } break;
        case IS8_TYPE: {
            if(value->is8[0] < param->lo_limit.is8[0] || value->is8[0] > param->hi_limit.is8[0]) {
                return false;
            }
        } break;
        case US16_TYPE: {
            if(value->us16[0] < param->lo_limit.us16[0] || value->us16[0] > param->hi_limit.us16[0]) {
                return false;
            }
        } break;
        case IS16_TYPE: {
            if(value->is16[0] < param->lo_limit.is16[0] || value->is16[0] > param->hi_limit.is16[0]) {
                return false;
            }
        } break;
        case US32_TYPE: {
            if(value->us32 < param->lo_limit.us32 || value->us32 > param->hi_limit.us32) {
                return false;
            }
        } break;
        case IS32_TYPE: {
            if(value->is32 < param->lo_limit.is32 || value->is32 > param->hi_limit.is32) {
                return false;
            }
        } break;
        case FL32_TYPE: {
            if(value->fl32 < param->lo_limit.fl32 || value->fl32 > param->hi_limit.fl32) {
                return false;
            }
        } break;
        case STRING_TYPE: {
            /* Unsupport */
            return false;
        } break;
    }
    return true;
}


static SYSTEM_ERROR_CODE_E generate_dev_name(void) {
    string_param_t *_name_p = &str_params[0];
    uint32_t _rn = gen_device_name_rn();
    char _name[DEV_NAME_SIZE];
    memcpy(_name, DEV_NAME_TEMPLATE, DEV_NAME_SIZE); 
    snprintf(&_name[DEV_NAME_SIZE - 7], 8, "%06lu", (unsigned long)(_rn % 1000000UL));
    return kv_set(_name_p->key, STRING_TYPE, (uint8_t *)_name, _name_p->val_max_len);
}


static SYSTEM_ERROR_CODE_E restore_all_params_to_default(void) {
    SYSTEM_ERROR_CODE_E _res = ERR_NONE;
    /* Restore Params */
    uint32_t _param_count = sizeof(numeric_params) / sizeof(numeric_param_t);
    for(uint32_t i = 0; i < _param_count; i++) {
        numeric_param_t *_p = &numeric_params[i];
        kv_set(_p->key, _p->type, (uint8_t *)&_p->default_val, sizeof(FORMAT_4BYTES_U));
        memcpy(&_p->cache, &_p->default_val, sizeof(FORMAT_4BYTES_U));
        /* Set cfg-set of param */
        kv_link_cfg_info(_p->key, _p);
    }
    /* Regenerate device name */
    generate_dev_name();
    
    /* Restore String */
    uint32_t _str_count = sizeof(str_params) / sizeof(string_param_t);
    for(uint32_t i = 1; i < _str_count; i++) {
        string_param_t *_s = &str_params[i];
        kv_set(_s->key, STRING_TYPE, (uint8_t *)_s->default_val, strlen(_s->default_val));
    }
    
    /* Update change to new store */
    _res = create_new_active_store();
    if(_res == ERR_NONE) {
        uint32_t _write_count = get_ext_flash_write_count();
        dev_var_set(EXT_FLASH_WRITE_COUNT_INDEX, (FORMAT_4BYTES_U *)&_write_count);
        DEV_CONFIG_LOG_INFO("[restore] success");
    }
    else {
        DEV_CONFIG_LOG_ERROR("[restore] fail, error code: %d", _res);
    }
    return _res;
}


static SYSTEM_ERROR_CODE_E erase_all_config(void) {
    SYSTEM_ERROR_CODE_E _res = erase_all_params_sector();
    if(_res == ERR_NONE) {
        DEV_CONFIG_LOG_INFO("Erase all params success!");
    }
    return _res;
}


static SYSTEM_ERROR_CODE_E store_kv(void) {
    SYSTEM_ERROR_CODE_E _res = create_new_active_store();
    if(_res == ERR_NONE) {
        uint32_t _write_count = get_ext_flash_write_count();
        dev_var_set(EXT_FLASH_WRITE_COUNT_INDEX, (FORMAT_4BYTES_U *)&_write_count);
        DEV_CONFIG_LOG_INFO("[store] success");
    }
    else {
        DEV_CONFIG_LOG_ERROR("[store] fail, error code: %d", _res);
    }
    return _res;
}


SYSTEM_ERROR_CODE_E get_kv_item_type(const char *key, uint8_t *type) {
    SYSTEM_ERROR_CODE_E _res = ERR_NONE;
    if(NULL == key || NULL == type) {
        return ERR_INVALID_POINTER;
    }
    _res = kv_get_type(key, type);
    return _res;
}


SYSTEM_ERROR_CODE_E get_kv_item_len(const char *key, uint16_t *len) {
    SYSTEM_ERROR_CODE_E _res = ERR_NONE;
    if(NULL == key || NULL == len) {
        return ERR_INVALID_POINTER;
    }
    _res = kv_get_len(key, len);
    return _res;
}


SYSTEM_ERROR_CODE_E get_kv_param_value(const char *key, void *value) {
    SYSTEM_ERROR_CODE_E _res = ERR_NONE;
    numeric_param_t *_cfg_info = NULL;
    _res = kv_get_cfg_info(key, (void **)&_cfg_info);
    if(_res != ERR_NONE) {
        return _res;
    }
    if(_cfg_info == NULL) {
        return ERR_NOT_EXIST;
    }
    /* Check limit range */
    if(false == check_params_validation(_cfg_info, &_cfg_info->cache)) {
        return ERR_OUT_OF_RANGE;
    }
    /* Use data from SRAM cache instead of directly reading it */
    switch(_cfg_info->type) {
        case US8_TYPE: {
            uint8_t *_us8 = (uint8_t *)value;
            *_us8 = _cfg_info->cache.us8[0];
        } break;
        case IS8_TYPE: {
            int8_t *_is8 = (int8_t *)value;
            *_is8 = _cfg_info->cache.is8[0];
        } break;
        case US16_TYPE: {
            uint16_t *_us16 = (uint16_t *)value;
            *_us16 = _cfg_info->cache.us16[0];
        } break;
        case IS16_TYPE: {
            int16_t *_is16 = (int16_t *)value;
            *_is16 = _cfg_info->cache.is16[0];
        } break;
        case US32_TYPE: {
            uint32_t *_us32 = (uint32_t *)value;
            *_us32 = _cfg_info->cache.us32;
        } break;
        case IS32_TYPE: {
            int32_t *_is32 = (int32_t *)value;
            *_is32 = _cfg_info->cache.is32;
        } break;
        case FL32_TYPE: {
            float *_fl32 = (float *)value;
            *_fl32 = _cfg_info->cache.fl32;
        } break;
    }
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E set_kv_param_value(const char *key, void *value) {
    SYSTEM_ERROR_CODE_E _res = ERR_NONE;
    numeric_param_t *_cfg_info = NULL;
    _res = kv_get_cfg_info(key, (void **)&_cfg_info);
    if(_res != ERR_NONE) {
        return _res;
    }
    if(_cfg_info == NULL) {
        return ERR_NOT_EXIST;
    }
    /* Check limit range */
    if(false == check_params_validation(_cfg_info, value)) {
        return ERR_OUT_OF_RANGE;
    }
    /* Sync to cache */
    memset(&_cfg_info->cache, 0, sizeof(FORMAT_4BYTES_U));
    switch(_cfg_info->type) {
        case US8_TYPE: {
            uint8_t *_us8 = (uint8_t *)value;
            _cfg_info->cache.us8[0] = *_us8;
        } break;
        case IS8_TYPE: {
            int8_t *_is8 = (int8_t *)value;
            _cfg_info->cache.is8[0] = *_is8;
        } break;
        case US16_TYPE: {
            uint16_t *_us16 = (uint16_t *)value;
            _cfg_info->cache.us16[0] = *_us16;
        } break;
        case IS16_TYPE: {
            int16_t *_is16 = (int16_t *)value;
            _cfg_info->cache.is16[0] = *_is16;
        } break;
        case US32_TYPE: {
            uint32_t *_us32 = (uint32_t *)value;
            _cfg_info->cache.us32 = *_us32;
        } break;
        case IS32_TYPE: {
            int32_t *_is32 = (int32_t *)value;
            _cfg_info->cache.is32 = *_is32;
        } break;
        case FL32_TYPE: {
            float *_fl32 = (float *)value;
            _cfg_info->cache.fl32 = *_fl32;
        } break;
    }
    /* Store to kv entry */
    _res = kv_set(key, _cfg_info->type, value, sizeof(FORMAT_4BYTES_U));
    return _res;
}


SYSTEM_ERROR_CODE_E get_kv_str_value(const char *key, char *value, uint16_t len) {
    uint8_t type = UNDEFINE_TYPE;
    SYSTEM_ERROR_CODE_E _res = kv_get_type(key, &type);
    if(ERR_NONE != _res) {
        return _res;
    }
    if(type != STRING_TYPE) {
        return ERR_INVALID_TYPE;
    }
    _res = kv_get(key, (uint8_t *)value, len);
    return _res;
}


SYSTEM_ERROR_CODE_E set_kv_str_value(const char *key, char *value, uint16_t len) {
    return kv_set(key, STRING_TYPE, (uint8_t *)value, len);
}


SYSTEM_ERROR_CODE_E delete_kv_item(const char *key) {
    return kv_delete(key);
}


SYSTEM_ERROR_CODE_E dev_store_init(void) {
    SYSTEM_ERROR_CODE_E _res = ERR_NONE;
    bool _need_store = false;
    ext_flash_handler_t _flash_handler = {
        .read = dev_sfud_mgnt_read,
        .write = dev_sfud_mgnt_write,
        .erase = dev_sfud_mgnt_erase
    };
    _res = ext_store_init(&_flash_handler);
    if(_res != ERR_NONE) {
        DEV_CONFIG_LOG_ERROR("Ext store init fail, code: %d", _res);
        return _res;
    }
    /* Find active and backup sectors */
    _res = load_active_store_from_ext_flash();
    if(_res != ERR_NOT_EXIST && _res != ERR_NONE) {
        DEV_CONFIG_LOG_ERROR("Load active store fail, code: %d", _res);
        return _res;
    }
    /* Create kv hash table */
    SYSTEM_ERROR_CODE_E _kv_init_res = kv_store_init();
    if(_kv_init_res != ERR_NONE) {
        DEV_CONFIG_LOG_ERROR("Create kv hash table fail, code: %d", _kv_init_res);
        return _kv_init_res;
    }
    if(_res == ERR_NOT_EXIST) {
        /* Both Active and Backup are Not exist: Need restore to default */
        _res = restore_all_params_to_default();
        if(_res != ERR_NONE) {
            return _res;
        }
        /* Create double sectors */
        _need_store = true;
    } else {
        uint32_t _param_count = sizeof(numeric_params) / sizeof(numeric_param_t);
        for(uint32_t i = 0; i < _param_count; i++) {
            numeric_param_t *_p = &numeric_params[i];
            /* Check params exist */
            FORMAT_4BYTES_U _value;
            SYSTEM_ERROR_CODE_E _kv_ret = kv_get(_p->key, (uint8_t *)&_value, sizeof(FORMAT_4BYTES_U));
            if(_kv_ret != ERR_NOT_EXIST && _kv_ret != ERR_NONE) {
                DEV_CONFIG_LOG_ERROR("Get kv data fail, code: %d", _kv_ret);
                return _kv_ret;
            }
            if(ERR_NOT_EXIST == _kv_ret) {
                /* Not exist: append default value */
                kv_set(_p->key, _p->type, (uint8_t *)&_p->default_val, sizeof(FORMAT_4BYTES_U));
                memcpy(&_p->cache, &_p->default_val, sizeof(FORMAT_4BYTES_U));
                _need_store = true;
            }
            /* Check params validation */
            else if(false == check_params_validation(_p, &_value)) {
                /* Outside legal range: set default value */
                kv_set(_p->key, _p->type, (uint8_t *)&_p->default_val, sizeof(FORMAT_4BYTES_U));
                memcpy(&_p->cache, &_p->default_val, sizeof(FORMAT_4BYTES_U));
                _need_store = true;
            }
            else {
                /* Sync param to SRAM cache */
                memcpy(&_p->cache, &_value, sizeof(FORMAT_4BYTES_U));
            }
            /* Set cfg-set of param */
            kv_link_cfg_info(_p->key, _p);
        }
        /* Check dev name */
        char _name[DEV_NAME_SIZE] = {0};
        SYSTEM_ERROR_CODE_E _name_ret = kv_get(str_params[0].key, (uint8_t *)_name, DEV_NAME_SIZE);
        if(_name_ret != ERR_NOT_EXIST && _name_ret != ERR_NONE) {
            DEV_CONFIG_LOG_ERROR("Get dev name fail, code: %d", _name_ret);
            return _name_ret;
        }
        if(ERR_NOT_EXIST == _name_ret) {
            generate_dev_name();
            _need_store = true;
        }
        else if(0 != memcmp(_name, DEV_NAME_TEMPLATE, DEV_NAME_SIZE - 6)) {
            generate_dev_name();
            _need_store = true;
        }
        
        /* Check string exist */
        uint32_t _str_count = sizeof(str_params) / sizeof(string_param_t);
        for(uint32_t i = 1; i < _str_count; i++) {
            string_param_t *_s = &str_params[i];
            uint16_t _len = 0;
            SYSTEM_ERROR_CODE_E _kv_ret = get_kv_item_len(_s->key, &_len);
            if( _kv_ret != ERR_NONE) {
                kv_set(_s->key, STRING_TYPE, (uint8_t *)_s->default_val, strlen(_s->default_val));
                _need_store = true;
            }
        }
        
        /* Is any sector missing */
        if(is_active_or_backup_missing()) {
            _need_store = true;
        }
    }
    if(_need_store) {
        _res = create_new_active_store();
        if(_res != ERR_NONE) {
            return _res;
        }
    }
    uint32_t _write_count = get_ext_flash_write_count();
    dev_var_set(EXT_FLASH_WRITE_COUNT_INDEX, (FORMAT_4BYTES_U *)&_write_count);
    return _res;
}


SYSTEM_ERROR_CODE_E get_uicr_sn128(uint8_t *sn128) {
    if(NULL == sn128) {
        return ERR_INVALID_POINTER;
    }
    memcpy(sn128, s_cfg_mgnt.uicr_data.sn128, SN128_BYTESNUM);
    return ERR_NONE;
}


void register_ext_flash_uid(void) {
    sfud_flash *_ext_flash = sfud_get_device(0);
    if(NULL == _ext_flash) {
        DEV_CONFIG_LOG_ERROR("No external flash detected!");
        return;
    }
    FORMAT_8BYTES_U _ext_uid = {0};
    _ext_uid.us64 = _ext_flash->chip.unique_id;
    dev_var_set(EXT_FLASH_UID_LO_INDEX, (FORMAT_4BYTES_U *)&_ext_uid.us32[0]);
    dev_var_set(EXT_FLASH_UID_HI_INDEX, (FORMAT_4BYTES_U *)&_ext_uid.us32[1]);
}


void dev_store_cfg_init(void) {
    memset(&s_cfg_mgnt, 0, sizeof(config_mgnt_t));
	/* SFUD initialize */
    ret_code_t _sfud_res = dev_sfud_mgnt_init();
    if(_sfud_res != NS_SUCCESS) {
        APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL + _sfud_res);
    }
    /* Get ext-flash uid */
    register_ext_flash_uid();
    
    SYSTEM_ERROR_CODE_E _store_res = dev_store_init();
    if(_store_res != ERR_NONE) {
        APP_ERROR_HANDLER(BOOKOO_ERROR_SFUD_INIT_FAIL - _store_res);
    }
	/* UICR user datapack */
	nrf_uicr_dataget(&s_cfg_mgnt.uicr_data);
}


SYSTEM_ERROR_CODE_E need_restore_factory(void) {
    if(NULL == s_cfg_mgnt.event) {
        return ERR_NOT_EXIST;
    }
    xEventGroupSetBits(s_cfg_mgnt.event, NEED_RESTORE_CONFIG_BIT);
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E need_write_config(void) {
    if(NULL == s_cfg_mgnt.event) {
        return ERR_NOT_EXIST;
    }
    taskENTER_CRITICAL();
    s_store_request_seq++;
    taskEXIT_CRITICAL();
    xEventGroupSetBits(s_cfg_mgnt.event, NEED_WRITE_CONFIG_BIT);
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E wait_config_store(void) {
    if(NULL == s_cfg_mgnt.event) {
        return ERR_NOT_EXIST;
    }
    const uint32_t _request_seq = s_store_request_seq;
    const TickType_t _start_tick = xTaskGetTickCount();
    while(s_store_complete_seq < _request_seq) {
        if((xTaskGetTickCount() - _start_tick) >= pdMS_TO_TICKS(CONFIG_STORE_WAIT_TIMEOUT_MS)) {
            return ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return s_store_result;
}


SYSTEM_ERROR_CODE_E need_erase_all(void) {
    if(NULL == s_cfg_mgnt.event) {
        return ERR_NOT_EXIST;
    }
    xEventGroupSetBits(s_cfg_mgnt.event, NEED_ERASE_ALL_CONFIG_BIT);
    return ERR_NONE;
}


void store_task(void *argument) {
    s_cfg_mgnt.event = xEventGroupCreate();
    os_task_info_t *_task_info = (os_task_info_t *)argument;
    s_cfg_mgnt.store_os = &_task_info[OS_TASK_STORE];
    while(true) {
        EventBits_t result = xEventGroupWaitBits(s_cfg_mgnt.event, NEED_WRITE_CONFIG_BIT | NEED_RESTORE_CONFIG_BIT | NEED_ERASE_ALL_CONFIG_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        if(result & NEED_ERASE_ALL_CONFIG_BIT) {
            erase_all_config();
            vTaskDelay(pdMS_TO_TICKS(10));
            __set_PRIMASK(1);
            NVIC_SystemReset();
        } 
        else if(result & NEED_RESTORE_CONFIG_BIT) {
            if(ERR_NONE == restore_all_params_to_default()) {
                vTaskDelay(pdMS_TO_TICKS(10));
                __set_PRIMASK(1);
                NVIC_SystemReset();
            }
        }
        else if(result & NEED_WRITE_CONFIG_BIT) {
            uint32_t _request_seq = s_store_request_seq;
            s_store_result = store_kv();
            s_store_complete_seq = _request_seq;
        }
        s_cfg_mgnt.store_os->wake_tick = xTaskGetTickCount();
        vTaskDelayUntil(&s_cfg_mgnt.store_os->wake_tick, pdMS_TO_TICKS(TASK_STORE_INTERVAL_MS));
    }
}


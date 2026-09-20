#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "default_parameters.h"
#include "auto_version.h"
#include "dev_vars_table.h"
#include "log.h"
#include "main.h"
#include "dev_status.h"
#include "n32l40x_dbg.h"


static struct {
    char version_name[_MAX_LEN_];
    char commit_ts[_MAX_LEN_];
    char last_compile_ts[_MAX_LEN_];
    char last_author[_MAX_LEN_];
} s_fw_info_t;

static status_item_t s_dev_status[STATUS_TYPE_MAX_TOTAL_COUNT];
static SemaphoreHandle_t s_status_mutex = NULL;


#define DEV_STATUS_LOG_ERROR(...)    LOG_ERROR("dev_status", __VA_ARGS__)
#define DEV_STATUS_LOG_INFO(...)     LOG_INFO("dev_status", __VA_ARGS__)

static SYSTEM_ERROR_CODE_E dev_var_set_us32(VARS_CACHE_INDEX_E index, uint32_t value) {
    FORMAT_4BYTES_U var_value = { 0 };
    var_value.us32 = value;
    return dev_vars_table_set(index, &var_value);
}


SYSTEM_ERROR_CODE_E dev_status_init(void) {
    if(strlen(TAG_VERSION) >= _MAX_LEN_ 
        || strlen(COMMIT_TIME) >= _MAX_LEN_ 
        || strlen(LAST_COMPILE_TIME) >= _MAX_LEN_ 
        || strlen(LAST_COMPILE_EMAIL) >= _MAX_LEN_) {
        return ERR_INVALID_LEN;
    }
        
    SYSTEM_ERROR_CODE_E ret = dev_vars_table_init();
    if (ret != ERR_NONE) {
        goto mem_fail;
    }
    
    snprintf(s_fw_info_t.version_name, sizeof(s_fw_info_t.version_name), "%s", TAG_VERSION);
    snprintf(s_fw_info_t.commit_ts, sizeof(s_fw_info_t.commit_ts), "%s", COMMIT_TIME);
    snprintf(s_fw_info_t.last_compile_ts, sizeof(s_fw_info_t.last_compile_ts), "%s", LAST_COMPILE_TIME);
    snprintf(s_fw_info_t.last_author, sizeof(s_fw_info_t.last_author), "%s", LAST_COMPILE_EMAIL);
    dev_var_set_us32(COMPILE_VERSION_INDEX, (uint32_t)s_fw_info_t.version_name);
    dev_var_set_us32(LATEST_COMMIT_TS_INDEX, (uint32_t)s_fw_info_t.commit_ts);
    dev_var_set_us32(LATEST_COMPILE_TS_INDEX, (uint32_t)s_fw_info_t.last_compile_ts);
    dev_var_set_us32(LATEST_AUTHOR_INDEX, (uint32_t)s_fw_info_t.last_author);

    uint8_t uid[UID_LENGTH];
    GetUID(uid);
    uint32_t uid_low = (uint32_t)uid[0] | ((uint32_t)uid[1] << 8) |
                       ((uint32_t)uid[2] << 16) | ((uint32_t)uid[3] << 24);
    uint32_t uid_high = (uint32_t)uid[4] | ((uint32_t)uid[5] << 8) |
                        ((uint32_t)uid[6] << 16) | ((uint32_t)uid[7] << 24);
    dev_var_set_us32(CHIP_UID_LO_INDEX, uid_low);
    dev_var_set_us32(CHIP_UID_HI_INDEX, uid_high);
    s_status_mutex = xSemaphoreCreateMutex();
    if (s_status_mutex == NULL) {
        DEV_STATUS_LOG_ERROR("Failed to create mutex!");
        return ERR_NO_MEM;
    }
    memset(s_dev_status, 0, sizeof(status_item_t) * STATUS_TYPE_MAX_TOTAL_COUNT);
    DEV_STATUS_LOG_INFO("Device status module initialized");
    return ERR_NONE;
    
mem_fail:
    DEV_STATUS_LOG_ERROR("Failed to create mutex/table!");
    dev_vars_table_deinit();
    if(s_status_mutex) {
        vSemaphoreDelete(s_status_mutex);
    }
    return ERR_NO_MEM;    
}


SYSTEM_ERROR_CODE_E dev_status_set(DEV_STATUS_TYPE_E status_type, uint32_t new_status) {
    if (status_type >= STATUS_TYPE_MAX_TOTAL_COUNT) {
        return ERR_INVALID_ARG;
    }
    if (s_status_mutex == NULL) {
        return ERR_INVALID_POINTER;
    }
    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) != pdTRUE) {
        DEV_STATUS_LOG_ERROR("Failed to take mutex!");
        return ERR_INVALID_STATE;
    }

    s_dev_status[status_type].value = new_status;
    s_dev_status[status_type].timestamp = xTaskGetTickCount();

    xSemaphoreGive(s_status_mutex);
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E dev_status_get(DEV_STATUS_TYPE_E status_type, uint32_t *out_status) {
    if (status_type >= STATUS_TYPE_MAX_TOTAL_COUNT || out_status == NULL) {
        return ERR_INVALID_ARG;
    }
    if (s_status_mutex == NULL) {
        return ERR_INVALID_POINTER;
    }
    if (xSemaphoreTake(s_status_mutex, portMAX_DELAY) != pdTRUE) {
        DEV_STATUS_LOG_ERROR("Failed to take mutex!");
        return ERR_INVALID_STATE;
    }

    *out_status = s_dev_status[status_type].value;

    xSemaphoreGive(s_status_mutex);
    return ERR_NONE;
}

SYSTEM_ERROR_CODE_E dev_var_set(VARS_CACHE_INDEX_E index, FORMAT_4BYTES_U *value) {

    SYSTEM_ERROR_CODE_E ret = dev_vars_table_set(index, value);
    if (ret == ERR_INVALID_STATE) {
        DEV_STATUS_LOG_ERROR("Failed to take vars sub-table mutex! %d", index);
    }
    return ret;
}


SYSTEM_ERROR_CODE_E dev_var_get(VARS_CACHE_INDEX_E index, FORMAT_4BYTES_U *out_value) {

    SYSTEM_ERROR_CODE_E ret = dev_vars_table_get(index, out_value);
    if (ret == ERR_INVALID_STATE) {
        DEV_STATUS_LOG_ERROR("Failed to take vars sub-table mutex! %d", index);
    }
    return ret;
}


void dev_sw_reset(void) {
    __set_PRIMASK(1);
    NVIC_SystemReset();
}


void dev_hw_poweron(void) {
    GPIO_SetBits(HW_POWERON_GPIO_Port, HW_POWERON_Pin);
}


void dev_hw_shutdown(void) {
    GPIO_ResetBits(HW_POWERON_GPIO_Port, HW_POWERON_Pin);
}

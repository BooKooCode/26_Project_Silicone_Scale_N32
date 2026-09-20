#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "SEGGER_RTT.h"
#include "auto_event_handler.h"
#include "double_list.h"
#include "app_freertos.h"
#include "main.h"
#include "tim.h"
#include "os_monitor.h"
#include "default_parameters.h"
#include "log.h"



#if STATIC_DEBUG
#define __STATIC__
#else
#define __STATIC__  static
#endif


#ifndef LOG_ENABLE
    #define LOG_ENABLE                          0
#endif

#ifndef LOG_UPLOAD_ENABLE
    #define LOG_UPLOAD_ENABLE                   0
#endif

#if (CODE_ROLE == BOOTLOADER_ROLE)
  #ifndef LOG_DOWNLOAD_ENABLE
    #define LOG_DOWNLOAD_ENABLE                 0
  #endif
#else
  #ifndef LOG_DOWNLOAD_ENABLE
        #define LOG_DOWNLOAD_ENABLE                 0
  #endif
#endif

#ifndef LOG_DISPLAY_COLOR
    #define LOG_DISPLAY_COLOR                   0
#endif

#ifndef LOG_DISPLAY_TIMESTAMP
    #define LOG_DISPLAY_TIMESTAMP               0
#endif

#ifndef LOG_TIMESTAMP_PRECISION
    #define LOG_TIMESTAMP_PRECISION             0
#endif

#ifndef LOG_TIMESTAMP_STR_MAX_LEN
    #define LOG_TIMESTAMP_STR_MAX_LEN           24U
#endif

#ifndef LOG_RTT_CHANNEL
    #define LOG_RTT_CHANNEL                     0
#endif

#ifndef LOG_TX_QUEUE_MESSAGE_COUNT
    #define LOG_TX_QUEUE_MESSAGE_COUNT          32U
#endif

#ifndef LOG_RX_SCAN_PERIOD_MS
    #define LOG_RX_SCAN_PERIOD_MS               5U
#endif

#ifndef LOG_RX_PROCESS_HANDLER_MAX_COUNT
    #define LOG_RX_PROCESS_HANDLER_MAX_COUNT    10U
#endif

#ifndef LOG_RX_PER_PROCESS_MAX_LENGTH
    #define LOG_RX_PER_PROCESS_MAX_LENGTH       64U
#endif


#if LOG_DISPLAY_COLOR
    #define SYSTEM_LOG_CLC(arg...)      SEGGER_RTT_printf(LOG_RTT_CHANNEL, RTT_CTRL_CLEAR arg)
    #define SYSTEM_RAW_INFO(arg...)     SEGGER_RTT_printf(LOG_RTT_CHANNEL, RTT_CTRL_RESET arg)
    #define SYSTEM_INFO(arg...)         SEGGER_RTT_printf(LOG_RTT_CHANNEL, RTT_CTRL_TEXT_GREEN arg)
    #define SYSTEM_WARN(arg...)         SEGGER_RTT_printf(LOG_RTT_CHANNEL, RTT_CTRL_TEXT_YELLOW arg)
    #define SYSTEM_ERR(arg...)          SEGGER_RTT_printf(LOG_RTT_CHANNEL, RTT_CTRL_TEXT_RED arg)
    #define SYSTEM_DEBUG(arg...)        SEGGER_RTT_printf(LOG_RTT_CHANNEL, RTT_CTRL_TEXT_WHITE arg)
#else
    #define SYSTEM_LOG_CLC(arg...)      SEGGER_RTT_printf(LOG_RTT_CHANNEL, arg)
    #define SYSTEM_RAW_INFO(arg...)     SEGGER_RTT_printf(LOG_RTT_CHANNEL, RTT_CTRL_RESET arg)
    #define SYSTEM_INFO(arg...)         SEGGER_RTT_printf(LOG_RTT_CHANNEL, arg)
    #define SYSTEM_WARN(arg...)         SEGGER_RTT_printf(LOG_RTT_CHANNEL, arg)
    #define SYSTEM_ERR(arg...)          SEGGER_RTT_printf(LOG_RTT_CHANNEL, arg)
    #define SYSTEM_DEBUG(arg...)        SEGGER_RTT_printf(LOG_RTT_CHANNEL, arg)
#endif


#ifndef USE_RTOS_MEMORY
    #define USE_RTOS_MEMORY         0
#endif


const osThreadAttr_t log_tx_attributes = {
    .name = "log_tx",
    .stack_size = 160 * 4,
    .priority = (osPriority_t) osPriorityBelowNormal7,
};


const osThreadAttr_t log_rx_attributes = {
    .name = "log_rx",
    .stack_size = 320 * 4,
    .priority = (osPriority_t) osPriorityBelowNormal7,
};


const osMessageQueueAttr_t tx_queue_attributes = {
  .name = "log_tx_queue"
};


typedef struct {
    char *data;
#if LOG_DISPLAY_TIMESTAMP
    struct {
        uint32_t ms;
#if (LOG_TIMESTAMP_PRECISION == LOG_TS_PRECISION_US)
        uint16_t us;
#endif
    } time_stamp;
#endif
    uint16_t length;
    LOG_SEVERITY_E severity;
} log_content_t;


typedef struct {
    osThreadId_t tx_os_handle;
#if LOG_DOWNLOAD_ENABLE
    osThreadId_t rx_os_handle;
    osMutexId_t rx_resp_mutex;
#endif
    osMessageQueueId_t tx_queue;
    auto_handler rx_process_handler;
    char cur_buf[LOG_RX_PER_PROCESS_MAX_LENGTH];
    char remain_buf[LOG_RX_PER_PROCESS_MAX_LENGTH];
} log_mgnt_t;


__STATIC__ log_mgnt_t s_log_mgnt;


__attribute__((weak)) SYSTEM_ERROR_CODE_E append_os_record(os_index_t *_index, void *_os_handler, uint32_t _alive_ms) {
    (void)_os_handler;
    (void)_alive_ms;
    if(_index != NULL) {
        *_index = 0;
    }
    return ERR_NONE;
}


__attribute__((weak)) SYSTEM_ERROR_CODE_E update_os_thread_state(os_index_t _index) {
    (void)_index;
    return ERR_NONE;
}


/* Private function declarations ---------------------------------------------*/

static void* s_calloc(size_t num, size_t size) {
#if USE_RTOS_MEMORY
    void *addr = pvPortMalloc(num * size);
    if(addr == NULL) {
        return NULL;
    }
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


static inline void _format_timestamp(uint32_t _total_ms, uint16_t _us, char *_dist) {
    if(NULL == _dist) {
        return;
    }
    uint32_t total_ms = _total_ms;
    uint32_t total_sec = total_ms / 1000;
    uint16_t hh = total_sec / 3600;
    uint16_t mm = (total_sec % 3600) / 60;
    uint16_t ss = total_sec % 60;
    uint16_t ms = total_ms % 1000;

#if (LOG_TIMESTAMP_PRECISION == LOG_TS_PRECISION_S)
    snprintf(_dist, LOG_TIMESTAMP_STR_MAX_LEN, "[%03u:%02u:%02u]", hh % 24, mm, ss);
#elif (LOG_TIMESTAMP_PRECISION == LOG_TS_PRECISION_MS)
    snprintf(_dist, LOG_TIMESTAMP_STR_MAX_LEN, "[%03u:%02u:%02u.%03u]", hh % 24, mm, ss, ms);
#elif (LOG_TIMESTAMP_PRECISION == LOG_TS_PRECISION_US)
    snprintf(_dist, LOG_TIMESTAMP_STR_MAX_LEN, "[%03u:%02u:%02u.%03u.%03u]", hh % 24, mm, ss, ms, _us);
#endif
}


static void rtt_output(LOG_SEVERITY_E severity, uint32_t total_ms, uint16_t us, char *log) {
#if (LOG_ENABLE && LOG_UPLOAD_ENABLE)
    
#if LOG_DISPLAY_TIMESTAMP
    char _ts[LOG_TIMESTAMP_STR_MAX_LEN] = {0};
    _format_timestamp(total_ms, us, _ts);
#endif
    switch(severity) {
        case LOG_SEVERITY_ERROR:
        #if LOG_DISPLAY_TIMESTAMP
            SYSTEM_ERR("%s" "<error>" "%s\n", _ts, log);
        #else
            SYSTEM_ERR("<error>" "%s\n", log);
        #endif
            break;
        case LOG_SEVERITY_WARNING:
        #if LOG_DISPLAY_TIMESTAMP
            SYSTEM_WARN("%s" "<warning>" "%s\n", _ts, log);
        #else
            SYSTEM_WARN("<warning>" "%s\n", log);
        #endif
            break;
        case LOG_SEVERITY_DEBUG:
        #if LOG_DISPLAY_TIMESTAMP
            SYSTEM_DEBUG("%s" "<debug>" "%s\n", _ts, log);
        #else
            SYSTEM_DEBUG("<debug>" "%s\n", log);
        #endif
            break;
        case LOG_SEVERITY_INFO:
        #if LOG_DISPLAY_TIMESTAMP
            SYSTEM_INFO("%s" "<info>" "%s\n", _ts, log);
        #else
            SYSTEM_INFO("<info>" "%s\n", log);
        #endif
            break;
        case LOG_SEVERITY_RAW_INFO:
        #if LOG_DISPLAY_TIMESTAMP
            SYSTEM_RAW_INFO("%s" "<info>" "%s\n", _ts, log);
        #else
            SYSTEM_RAW_INFO("<info>" "%s\n", log);
        #endif
            break;
        case LOG_SEVERITY_CLC:
        #if LOG_DISPLAY_TIMESTAMP
            SYSTEM_LOG_CLC("%s" "<info>" "%s\n", _ts, log);
        #else
            SYSTEM_LOG_CLC("<info>" "%s\n", log);
        #endif
            break;
        default:
            break;
    }
#endif
}


#if LOG_UPLOAD_ENABLE
static SYSTEM_ERROR_CODE_E log_flush(log_content_t *_content) {
#if LOG_DISPLAY_TIMESTAMP
    if(_content == NULL) {
        return ERR_INVALID_POINTER;
    }
    if(_content->data == NULL) {
        return ERR_INVALID_ARG;
    }
#if (LOG_TIMESTAMP_PRECISION == LOG_TS_PRECISION_US)
    rtt_output(_content->severity, _content->time_stamp.ms, _content->time_stamp.us, _content->data);
#else
    rtt_output(_content->severity, _content->time_stamp.ms, 0, _content->data);
#endif
    s_free(_content->data);
    _content->data = NULL;
#endif
    return ERR_NONE;
}


static void log_tx_task(void *argument) {
    os_index_t os_index = 0;
    append_os_record(&os_index, s_log_mgnt.tx_os_handle, 0);
    
    osEventFlagsWait(os_init_eventHandle, LOG_INIT_BIT, osFlagsWaitAll | osFlagsNoClear, osWaitForever);
    log_content_t _tx_content = {0};
    while(true) {
        osMessageQueueGet(s_log_mgnt.tx_queue, &_tx_content, NULL, portMAX_DELAY);
        log_flush(&_tx_content);
        
        update_os_thread_state(os_index);
    }
}
#endif


#if (LOG_ENABLE && LOG_DOWNLOAD_ENABLE)
static int log_rx_mutex_lock(void *mutex) {
    if(mutex == NULL) {
        return -1;
    }
    return osMutexAcquire((osMutexId_t)mutex, osWaitForever);
}


static int log_rx_mutex_unlock(void *mutex) {
    if(mutex == NULL) {
        return -1;
    }
    return osMutexRelease((osMutexId_t)mutex);
}



static SYSTEM_ERROR_CODE_E log_process_str(void) {
    uint8_t _try_count = 0;
    size_t _remain_len = 0;
    memset(s_log_mgnt.cur_buf, 0, LOG_RX_PER_PROCESS_MAX_LENGTH);
    memset(s_log_mgnt.remain_buf, 0, LOG_RX_PER_PROCESS_MAX_LENGTH);
    while(SEGGER_RTT_HasData(LOG_RTT_CHANNEL) && (_try_count < 10)) {
        if(_remain_len > 0) {
            memcpy(s_log_mgnt.cur_buf, s_log_mgnt.remain_buf, _remain_len);
            memset(s_log_mgnt.remain_buf, 0, LOG_RX_PER_PROCESS_MAX_LENGTH);
        }
        size_t _rtt_len = (int)SEGGER_RTT_Read(0, &s_log_mgnt.cur_buf[_remain_len], (LOG_RX_PER_PROCESS_MAX_LENGTH - _remain_len));
        size_t _valid_len = strlen(s_log_mgnt.cur_buf);
        if(_rtt_len > (_valid_len + 1)) {
            _remain_len = _rtt_len - (_valid_len + 1);
            memcpy(s_log_mgnt.remain_buf, &s_log_mgnt.cur_buf[_valid_len + 1], _remain_len);
        }
        auto_event_run_handlers(s_log_mgnt.rx_process_handler, s_log_mgnt.cur_buf);
        _try_count ++;
    }
    return ERR_NONE;
}


static void log_rx_task(void *argument) {
    os_index_t os_index = 0;
    append_os_record(&os_index, s_log_mgnt.rx_os_handle, 0);
    
    osEventFlagsWait(os_init_eventHandle, LOG_INIT_BIT, osFlagsWaitAll | osFlagsNoClear, osWaitForever);
    uint32_t tick = osKernelGetTickCount();
    while(true) {
        vTaskDelayUntil(&tick, pdMS_TO_TICKS(LOG_RX_SCAN_PERIOD_MS));
        if(0 == SEGGER_RTT_HasData(LOG_RTT_CHANNEL)) {
            continue;
        }
        log_process_str();
        
        update_os_thread_state(os_index);
    }
}
#endif


/* Public function declarations ---------------------------------------------*/

SYSTEM_ERROR_CODE_E log_push(LOG_SEVERITY_E severity, const char* format, ...) {
#if (LOG_ENABLE && LOG_UPLOAD_ENABLE)
    if(NULL == s_log_mgnt.tx_queue) {
        return ERR_NOT_INIT;
    }
    va_list args;
	va_start(args, format);
    size_t _len = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if(0 == _len) {
        return ERR_INVALID_LEN;
    }
    log_content_t _content = {
        .length = _len + 1,
        .severity = severity,
        .data = NULL
    };
#if LOG_DISPLAY_TIMESTAMP
    _content.time_stamp.ms = (uint32_t)(((uint64_t)osKernelGetTickCount() * 1000U) /
                                       configTICK_RATE_HZ);
#if (LOG_TIMESTAMP_PRECISION == LOG_TS_PRECISION_US)
    _content.time_stamp.us = TIM_GetCnt(LOG_HIGH_PREC_TIM);
#endif
#endif
    _content.data = s_calloc(_content.length, sizeof(char));
    if(NULL == _content.data) {
        return ERR_NO_MEM;
    }
    va_start(args, format);
    vsnprintf(_content.data, _content.length, format, args);
    va_end(args);
    osStatus_t _stat = osMessageQueuePut(s_log_mgnt.tx_queue, &_content, 0, 0);
    if(_stat != osOK) {
        s_free(_content.data);
        return ERR_BUSY;
    }
#endif
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E register_log_rx_event(log_rx_process_handle_t _handle) {
    SYSTEM_ERROR_CODE_E _ret = ERR_NONE;
#if (LOG_ENABLE && LOG_DOWNLOAD_ENABLE)
    if(NULL == s_log_mgnt.rx_process_handler) {
        return ERR_NOT_INIT;
    }
    _ret = auto_event_register_handler(s_log_mgnt.rx_process_handler, _handle);
#endif
    return _ret;
}


SYSTEM_ERROR_CODE_E unregister_log_rx_event(log_rx_process_handle_t _handle) {
    SYSTEM_ERROR_CODE_E _ret = ERR_NONE;
#if (LOG_ENABLE && LOG_DOWNLOAD_ENABLE)
    if(NULL == s_log_mgnt.rx_process_handler) {
        return ERR_NOT_INIT;
    }
    _ret = auto_event_unregister_handler(s_log_mgnt.rx_process_handler, _handle);
#endif
    return _ret;
}


#if (LOG_ENABLE && LOG_DOWNLOAD_ENABLE)
static auto_event_custom_func_t s_custom_func = {
    .calloc = s_calloc,
    .free = s_free,
    .lock = log_rx_mutex_lock,
    .unlock = log_rx_mutex_unlock,
    .lock_data = NULL
};
#endif


SYSTEM_ERROR_CODE_E log_init(void) {
#if LOG_ENABLE
    
#if (LOG_TIMESTAMP_PRECISION == LOG_TS_PRECISION_US)
    LOG_HIGH_PREC_TIM_Init();
    TIM_Enable(LOG_HIGH_PREC_TIM, ENABLE);
#endif
#if (CODE_ROLE == BOOTLOADER_ROLE)
    /* Rtt hw init */
    SEGGER_RTT_Init();
#endif
    /* Tx and Rx thread init */
#if LOG_DOWNLOAD_ENABLE
    s_log_mgnt.rx_os_handle = osThreadNew(log_rx_task, NULL, &log_rx_attributes);
    if(NULL == s_log_mgnt.rx_os_handle) {
        return ERR_NO_MEM;
    }
    s_log_mgnt.rx_resp_mutex = osMutexNew(NULL);
    if(NULL == s_log_mgnt.rx_resp_mutex) {
        return ERR_NO_MEM;
    }
    s_custom_func.lock_data = s_log_mgnt.rx_resp_mutex;
    SYSTEM_ERROR_CODE_E _ret = auto_event_manager_init(&s_log_mgnt.rx_process_handler, &s_custom_func, LOG_RX_PROCESS_HANDLER_MAX_COUNT);
    if(ERR_NONE != _ret) {
        LOG_ERROR("log_init", "event manager init fail, error code: %d", _ret);
        return _ret;
    }
#endif

#if LOG_UPLOAD_ENABLE
    s_log_mgnt.tx_os_handle = osThreadNew(log_tx_task, NULL, &log_tx_attributes);
    if(NULL == s_log_mgnt.tx_os_handle) {
        return ERR_NO_MEM;
    }
    s_log_mgnt.tx_queue = osMessageQueueNew(LOG_TX_QUEUE_MESSAGE_COUNT, sizeof(log_content_t), &tx_queue_attributes);
    if(NULL == s_log_mgnt.tx_queue) {
        return ERR_NO_MEM;
    }
#endif
    
#endif
    osEventFlagsSet(os_init_eventHandle, LOG_INIT_BIT);
    return ERR_NONE;
}


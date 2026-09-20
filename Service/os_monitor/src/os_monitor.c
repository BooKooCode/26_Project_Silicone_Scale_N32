#include <string.h>
#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "projdefs.h"
#include "bookoo_error_def.h"
#include "queue.h"
#include "cmsis_compiler.h"
#include "app_freertos.h"
#include "os_monitor.h"
#include "log.h"
#include "default_parameters.h"


#ifndef USE_RTOS_MEMORY
#define USE_RTOS_MEMORY         0
#endif

#define TAG "OS_MGNT"


typedef struct {
    char *os_name;
    osThreadId_t os_handler;
    size_t free_heap;
    uint32_t exec_ts;
    osThreadState_t os_state;
    uint32_t alive_ms;
    uint8_t heartbeat_lost;
} os_record_t;

typedef struct {
    QueueHandle_t m_queue;
    const char *q_name;
    UBaseType_t warn_thres;
    UBaseType_t last_msgs;
    uint8_t overflowed;
} queue_record_t;

struct {
    size_t rtos_free_heap;
    size_t ever_min_free_heap;
    char overflow_task_name[configMAX_TASK_NAME_LEN];
    char malloc_fail_task_name[configMAX_TASK_NAME_LEN];
    uint16_t thread_count;
    os_record_t thread_record[OS_TASK_MAX_COUNT];
    uint16_t queue_count;
    queue_record_t queue_record[OS_QUEUE_MAX_COUNT];
    osMutexId_t mutex;
    osMessageQueueId_t idle_req_queue;
} s_os_mgnt;


#if RTOS_DEBUG
static void* s_calloc(size_t num, size_t size) {
#if USE_RTOS_MEMORY
    void *addr = pvPortMalloc(num * size);
    memset(addr, 0, num * size);
    return addr;
#else
    return calloc(num, size);
#endif
}


// static void s_free(void* addr) {
// #if USE_RTOS_MEMORY
//     vPortFree(addr);
// #else
//     free(addr);
// #endif
// }
#endif


SYSTEM_ERROR_CODE_E trans_os_idle_req(uint8_t req, uint32_t timeout) {
    osStatus_t _status = osMessageQueuePut(s_os_mgnt.idle_req_queue, &req, 0, pdMS_TO_TICKS(timeout));
    if(_status == osOK) {
        return ERR_NONE;
    }
    else if(_status == osErrorParameter) {
        return ERR_INVALID_ARG;
    }
    else if(_status == osErrorTimeout) {
        return ERR_TIMEOUT;
    }
    else if(_status == osErrorResource) {
        return ERR_BUSY;
    }
    else {
        return ERR_FAIL;
    }
}


void vApplicationIdleHook(void) {
    s_os_mgnt.rtos_free_heap = xPortGetFreeHeapSize();
    s_os_mgnt.ever_min_free_heap = xPortGetMinimumEverFreeHeapSize();
    uint8_t _idle_req = 0;
    osStatus_t _status = osMessageQueueGet(s_os_mgnt.idle_req_queue, &_idle_req, NULL, 0);
    if(_status != osOK) {
        return;
    }
    switch (_idle_req) {
        case OS_IDLE_REQ_SYS_RESET: {
            __set_PRIMASK(1);
            NVIC_SystemReset();
            break;
        }
        default:
            break;
    }
}


void vApplicationStackOverflowHook(xTaskHandle xTask, char *pcTaskName) {
    memset(s_os_mgnt.overflow_task_name, 0, configMAX_TASK_NAME_LEN);
	strcpy(s_os_mgnt.overflow_task_name, pcTaskName);
}


void vApplicationMallocFailedHook(void) {
    const char *name = pcTaskGetName(NULL);
    memset(s_os_mgnt.malloc_fail_task_name, 0, configMAX_TASK_NAME_LEN);
	strcpy(s_os_mgnt.malloc_fail_task_name, name);
}


void vApplicationDaemonTaskStartupHook(void) {
    
}


SYSTEM_ERROR_CODE_E append_os_record(os_index_t *_index, void *_os_handler, uint32_t _alive_ms) {
#if RTOS_DEBUG
    if(NULL == s_os_mgnt.mutex) {
        return ERR_NOT_INIT;
    }
    if(NULL == _index || NULL == _os_handler) {
        return ERR_INVALID_ARG;
    }
    if(s_os_mgnt.thread_count >= OS_TASK_MAX_COUNT) {
        return ERR_INVALID_LEN;
    }
    osMutexAcquire(s_os_mgnt.mutex, osWaitForever);
    *_index = s_os_mgnt.thread_count;
    s_os_mgnt.thread_record[*_index].os_handler = _os_handler;
    const char *_name = osThreadGetName(_os_handler);
    if(NULL == _name) {
        osMutexRelease(s_os_mgnt.mutex);
        return ERR_FAIL;
    }
    s_os_mgnt.thread_record[*_index].os_name = s_calloc(strlen(_name) + 1, sizeof(char));
    if(NULL == s_os_mgnt.thread_record[*_index].os_name) {
        osMutexRelease(s_os_mgnt.mutex);
        return ERR_NO_MEM;
    }
    memcpy(s_os_mgnt.thread_record[*_index].os_name, _name, strlen(_name));
    s_os_mgnt.thread_record[*_index].heartbeat_lost = 0;
    s_os_mgnt.thread_record[*_index].exec_ts = osKernelGetTickCount();
    s_os_mgnt.thread_record[*_index].alive_ms = _alive_ms;
    s_os_mgnt.thread_count += 1;
    osMutexRelease(s_os_mgnt.mutex);
#endif
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E update_os_thread_state(os_index_t _index) {
#if RTOS_DEBUG
    if(NULL == s_os_mgnt.mutex) {
        return ERR_NOT_INIT;
    }
    if(_index >= s_os_mgnt.thread_count) {
        return ERR_INVALID_LEN;
    }
    if(__get_IPSR()) {
        return ERR_INVALID_STATE;
    }
    if(osThreadTerminated == s_os_mgnt.thread_record[_index].os_state) {
        return ERR_INVALID_STATE;
    }
    osThreadId_t _thread = s_os_mgnt.thread_record[_index].os_handler;
    s_os_mgnt.thread_record[_index].os_state = osThreadGetState(_thread);
    if(osThreadTerminated != s_os_mgnt.thread_record[_index].os_state) {
        s_os_mgnt.thread_record[_index].free_heap = uxTaskGetStackHighWaterMark(_thread);
    }
    /* Internal heartbeat: update exec_ts when task is running (keeps internal liveness) */
    if(osThreadRunning == s_os_mgnt.thread_record[_index].os_state) {
        s_os_mgnt.thread_record[_index].exec_ts = osKernelGetTickCount();
    }
#endif
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E record_os_exit(os_index_t _index) {
#if RTOS_DEBUG
    if(NULL == s_os_mgnt.mutex) {
        return ERR_NOT_INIT;
    }
    if(_index >= s_os_mgnt.thread_count) {
        return ERR_INVALID_LEN;
    }
    if(__get_IPSR()) {
        return ERR_INVALID_STATE;
    }
    osMutexAcquire(s_os_mgnt.mutex, osWaitForever);
    s_os_mgnt.thread_record[_index].os_state = osThreadTerminated;
    osMutexRelease(s_os_mgnt.mutex);
#endif
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E append_queue_record(os_index_t *_index, void *_q_handler, uint32_t _warn_thres) {
#if RTOS_DEBUG
    if(NULL == s_os_mgnt.mutex) {
        return ERR_NOT_INIT;
    }
    if(NULL == _index || NULL == _q_handler) {
        return ERR_INVALID_ARG;
    }
    if(s_os_mgnt.queue_count >= OS_QUEUE_MAX_COUNT) {
        return ERR_INVALID_LEN;
    }
    osMutexAcquire(s_os_mgnt.mutex, osWaitForever);
    *_index = s_os_mgnt.queue_count;
    s_os_mgnt.queue_record[*_index].m_queue = _q_handler;
    s_os_mgnt.queue_record[*_index].q_name = pcQueueGetName(_q_handler);
    s_os_mgnt.queue_record[*_index].warn_thres = _warn_thres;
    s_os_mgnt.queue_record[*_index].last_msgs = 0;
    s_os_mgnt.queue_record[*_index].overflowed = 0;
    s_os_mgnt.queue_count += 1;
    osMutexRelease(s_os_mgnt.mutex);
#endif
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E update_queue_state(uint16_t _index) {
#if RTOS_DEBUG
    if(NULL == s_os_mgnt.mutex) {
        return ERR_NOT_INIT;
    }
    if(_index >= s_os_mgnt.queue_count) {
        return ERR_INVALID_LEN;
    }
    if(__get_IPSR()) {
        return ERR_INVALID_STATE;
    }
    QueueHandle_t queue_h = s_os_mgnt.queue_record[_index].m_queue;
    if(NULL == queue_h) {
        return ERR_NOT_INIT;
    }
    UBaseType_t capacity = s_os_mgnt.queue_record[_index].warn_thres;
    UBaseType_t msgs = osMessageQueueGetCount(queue_h);
    UBaseType_t threshold;
    if(capacity >= OS_QUEUE_MONITOR_MIN_CAPACITY) {
        UBaseType_t percent_based = (capacity * OS_QUEUE_OVERFLOW_THRESHOLD_PERCENT) / 100;
        threshold = (OS_QUEUE_OVERFLOW_THRESHOLD_MIN_COUNT > percent_based) ? OS_QUEUE_OVERFLOW_THRESHOLD_MIN_COUNT : percent_based;
    } 
    else {
        threshold = OS_QUEUE_OVERFLOW_THRESHOLD_MIN_COUNT;
    }
    osMutexAcquire(s_os_mgnt.mutex, osWaitForever);
    uint8_t prev_overflow = s_os_mgnt.queue_record[_index].overflowed;
    uint8_t cur_overflow = (msgs >= threshold) ? 1 : 0;
    
    const char *qname = s_os_mgnt.queue_record[_index].q_name[0] != '\0' ? s_os_mgnt.queue_record[_index].q_name : "unnamed";
    
    if(prev_overflow == 0 && cur_overflow == 1) {
        s_os_mgnt.queue_record[_index].overflowed = 1;
        osMutexRelease(s_os_mgnt.mutex);
        LOG_WARNING("OS_MGNT", "Queue overflow: idx=%u name=%s msgs=%u threshold=%u capacity=%u", 
                    _index, qname, (unsigned)msgs, (unsigned)threshold, (unsigned)capacity);
    } 
    else if(prev_overflow == 1 && cur_overflow == 0) {
        s_os_mgnt.queue_record[_index].overflowed = 0;
        osMutexRelease(s_os_mgnt.mutex);
        LOG_INFO("OS_MGNT", "Queue recovered: idx=%u name=%s msgs=%u threshold=%u capacity=%u", 
                 _index, qname, (unsigned)msgs, (unsigned)threshold, (unsigned)capacity);
    } 
    else {
        osMutexRelease(s_os_mgnt.mutex);
    }
    s_os_mgnt.queue_record[_index].last_msgs = msgs;
#endif
    return ERR_NONE;
}


void os_mgnt_timer(void *argument) {
#if RTOS_DEBUG
    if(NULL == s_os_mgnt.mutex) {
        return;
    }
    uint32_t now = osKernelGetTickCount();
    for(os_index_t i = 0; i < s_os_mgnt.thread_count; i++) {
        update_os_thread_state(i);
        if(osOK != osMutexAcquire(s_os_mgnt.mutex, 0)) {
            return;
        }
        uint8_t prev_hb = s_os_mgnt.thread_record[i].heartbeat_lost;
        if(s_os_mgnt.thread_record[i].alive_ms && s_os_mgnt.thread_record[i].exec_ts != 0) {
            uint32_t delta = (now >= s_os_mgnt.thread_record[i].exec_ts) ? (now - s_os_mgnt.thread_record[i].exec_ts) : (0xFFFFFFFFu - s_os_mgnt.thread_record[i].exec_ts + now + 1u);
            if(delta > s_os_mgnt.thread_record[i].alive_ms) {
                s_os_mgnt.thread_record[i].heartbeat_lost = 1;
            } 
            else {
                s_os_mgnt.thread_record[i].heartbeat_lost = 0;
            }
        }
        uint8_t cur_hb = s_os_mgnt.thread_record[i].heartbeat_lost;
        const char *tname = s_os_mgnt.thread_record[i].os_name ? s_os_mgnt.thread_record[i].os_name : "unknown";
        osMutexRelease(s_os_mgnt.mutex);
        if(prev_hb == 0 && cur_hb == 1) {
            LOG_WARNING(TAG, "Task heartbeat lost: idx=%u name=%s", (unsigned)i, tname);
        } 
        else if(prev_hb == 1 && cur_hb == 0) {
            LOG_INFO(TAG, "Task heartbeat recovered: idx=%u name=%s", (unsigned)i, tname);
        }
    }
    for(uint16_t q = 0; q < s_os_mgnt.queue_count; q++) {
        update_queue_state(q);
    }
#endif
}


#if RTOS_DEBUG
SYSTEM_ERROR_CODE_E os_mgnt_start_auto_check(void) {
    if(osKernelRunning != osKernelGetState()) {
        return ERR_INVALID_STATE;
    }
#ifdef OS_MGNT_TIMER
    osTimerStart(OS_MGNT_TIMER, OS_CHECK_PERIOD);
#endif
    return ERR_NONE;
}
#endif


SYSTEM_ERROR_CODE_E os_mgnt_init(void) {
#if RTOS_DEBUG
    memset(&s_os_mgnt, 0, sizeof(s_os_mgnt));
    s_os_mgnt.mutex = osMutexNew(NULL);
    if(NULL == s_os_mgnt.mutex) {
        return ERR_NO_MEM;
    }
#endif
    s_os_mgnt.idle_req_queue = osMessageQueueNew(OS_MGNT_REQ_QUEUE_SIZE, sizeof(uint8_t), NULL);
    if(NULL == s_os_mgnt.idle_req_queue) {
        return ERR_NO_MEM;
    }
    return ERR_NONE;
}





#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "double_list.h"
#include "auto_event_handler.h"

#ifndef USE_RTOS_MEMORY
#define USE_RTOS_MEMORY         0
#endif

typedef struct auto_handle_arg {
    size_t max_handle_length;
    list_t handle_list;
    SemaphoreHandle_t handle_mutex;
} auto_handle_arg_t;


static void* s_calloc(size_t num, size_t size) {
#if USE_RTOS_MEMORY
    void *addr = pvPortMalloc(num * size);
    memset(addr, 0, num * size);
    return addr;
#else
    return calloc(num, size);
#endif
}


static void s_free(void* addr) {
#if USE_RTOS_MEMORY
    vPortFree(addr);
#else
    free(addr);
#endif
}


int get_auto_event_handler_member_count(auto_handler handle_arg) {
    return get_list_length(handle_arg->handle_list);
}


SYSTEM_ERROR_CODE_E auto_event_register_handler(auto_handler handle_arg, auto_handle_event_t event_handler) {
    if(handle_arg->handle_list == NULL) {
        return ERR_INVALID_POINTER;
    }
    xSemaphoreTake(handle_arg->handle_mutex, portMAX_DELAY);
    int handleLength = get_auto_event_handler_member_count(handle_arg);
    if(handleLength >= handle_arg->max_handle_length) {
        xSemaphoreGive(handle_arg->handle_mutex);
        return ERR_NO_MEM;
    }
    append_list(handle_arg->handle_list, &event_handler);
    xSemaphoreGive(handle_arg->handle_mutex);
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E auto_event_unregister_handler(auto_handler handle_arg, auto_handle_event_t event_handler) {
    if(handle_arg->handle_list == NULL) {
        return ERR_INVALID_POINTER;
    }
    xSemaphoreTake(handle_arg->handle_mutex, portMAX_DELAY);
    iterator_t it = begin_of_list(handle_arg->handle_list);
    int count = get_auto_event_handler_member_count(handle_arg);
    for(int i = 0; i < count; i++) {
        auto_handle_event_t *handler_it = get_list_data(it);
        if(*handler_it == event_handler) {
            remove_iterator(handle_arg->handle_list, it);
            xSemaphoreGive(handle_arg->handle_mutex);
            return ERR_NONE;
        }
        it = get_list_next(it);
    }
    xSemaphoreGive(handle_arg->handle_mutex);
    return ERR_NOT_EXIST;
}


SYSTEM_ERROR_CODE_E auto_event_run_handlers(auto_handler handle_arg, void *input_data) {
    SYSTEM_ERROR_CODE_E result = 0xffff;
    xSemaphoreTake(handle_arg->handle_mutex, portMAX_DELAY);
    int handle_len = get_auto_event_handler_member_count(handle_arg);
    for(int i = 0; i < handle_len; i++) {
        auto_handle_event_t *handler = at_list(handle_arg->handle_list, i);
        /* As long as one of the functions returns 0, that means the overall response is successful */
        result &= (*handler)(input_data);
    }
    xSemaphoreGive(handle_arg->handle_mutex);
    return result;
}


SYSTEM_ERROR_CODE_E auto_event_manager_init(auto_handler *handle_arg, size_t max_length) {
    *handle_arg = s_calloc(1, sizeof(auto_handle_arg_t));
    if(*handle_arg == NULL) {
        return ERR_INVALID_POINTER;
    }
    (*handle_arg)->max_handle_length = max_length;
    (*handle_arg)->handle_mutex = xSemaphoreCreateMutex();
    if(NULL == (*handle_arg)->handle_mutex) {
        return ERR_NO_MEM;
    }
    xSemaphoreTake((*handle_arg)->handle_mutex, portMAX_DELAY);
    SYSTEM_ERROR_CODE_E err = init_list(&(*handle_arg)->handle_list, 
                                        sizeof(auto_handle_event_t),
                                        s_calloc,
                                        s_free);
    xSemaphoreGive((*handle_arg)->handle_mutex);
    return err;
}


void auto_event_manager_deinit(auto_handler *handle_arg) {
    destroy_list(&(*handle_arg)->handle_list);
    vSemaphoreDelete((*handle_arg)->handle_mutex);
    s_free(*handle_arg);
}

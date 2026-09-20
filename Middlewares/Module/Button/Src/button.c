#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "button.h"


#ifndef USE_RTOS_MEMORY
#define USE_RTOS_MEMORY         0
#endif


typedef struct {
    uint8_t target_count;
    uint8_t valid_level;
} multi_config_t;


typedef struct {
    SemaphoreHandle_t handle_mutex;
    
    bool last_trigger;
    uint32_t press_count;
	/* Detect startup press */
	bool is_startup;
	bool startup_press_valid;
    uint32_t last_release_tick;
	/* Detect multiple click */
    uint32_t multi_click_valid_count;
    uint8_t consecutive_clicks;
    list_t multi_conf_list;
    size_t multi_event_max_count;
    iterator_t curr_multi_event_item;
} button_statistic_t;


#define GET_BTN_STAT(btn_mgnt, index)       (((button_statistic_t *)(btn_mgnt)->btn_statis) + (index))


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


SYSTEM_ERROR_CODE_E register_button_response_event(btn_event_mgnt_t *btn_mgnt, uint8_t button_index, button_response_handle_t response_handle) {
    return auto_event_register_handler(btn_mgnt->btn_event_handler[button_index], response_handle);
}


SYSTEM_ERROR_CODE_E unregister_button_response_event(btn_event_mgnt_t *btn_mgnt, uint8_t button_index, button_response_handle_t response_handle) {
    return auto_event_unregister_handler(btn_mgnt->btn_event_handler[button_index], response_handle);
}


static SYSTEM_ERROR_CODE_E insert_multi_click_event(button_statistic_t *stat, uint8_t target_count, uint8_t valid_level) {
    xSemaphoreTake(stat->handle_mutex, portMAX_DELAY);
    iterator_t it = begin_of_list(stat->multi_conf_list);
    iterator_t end = end_of_list(stat->multi_conf_list);
    int count = get_list_length(stat->multi_conf_list);
    /* Overflow */
    if(count >= stat->multi_event_max_count) {
        xSemaphoreGive(stat->handle_mutex);
        return ERR_OUT_OF_RANGE;
    }
    multi_config_t _data = {0};
    _data.target_count = target_count;
    _data.valid_level = valid_level;
    if(count == 0) {
        if(end == append_list(stat->multi_conf_list, &_data)) {
            xSemaphoreGive(stat->handle_mutex);
            return ERR_NO_MEM;
        }
    } else {
        for(int i = 0; i < count; i++) {
            multi_config_t *_cfg = get_list_data(it);
            if(_cfg->target_count == target_count) {
                xSemaphoreGive(stat->handle_mutex);
                return ERR_REINIT;
            }
            else if(_cfg->target_count > target_count) {
                break;
            }
            it = get_list_next(it);
        }
        if(end == insert_list(stat->multi_conf_list, &_data, it)) {
            xSemaphoreGive(stat->handle_mutex);
            return ERR_NO_MEM;
        }
    }
    xSemaphoreGive(stat->handle_mutex);
    return ERR_NONE;
    
}


static SYSTEM_ERROR_CODE_E delete_multi_click_event(button_statistic_t *stat, uint8_t target_count) {
    xSemaphoreTake(stat->handle_mutex, portMAX_DELAY);
    iterator_t it = begin_of_list(stat->multi_conf_list);
    iterator_t end = end_of_list(stat->multi_conf_list);
    int count = get_list_length(stat->multi_conf_list);
    if(count == 0) {
        xSemaphoreGive(stat->handle_mutex);
        return ERR_NOT_EXIST;
    }
    for(int i = 0; i < count; i++) {
        multi_config_t *_cfg = get_list_data(it);
        if(it == end) {
            xSemaphoreGive(stat->handle_mutex);
            return ERR_NOT_EXIST;
        }
        if(_cfg->target_count == target_count) {
            remove_iterator(stat->multi_conf_list, it);
            break;
        }
        it = get_list_next(it);
    }
    xSemaphoreGive(stat->handle_mutex);
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E register_multi_click_event(btn_event_mgnt_t *btn_mgnt, uint8_t button_index, uint8_t target_count, uint8_t valid_level) {
    if(target_count < 3) {
        return ERR_INVALID_ARG;
    }
    button_statistic_t *stat = GET_BTN_STAT(btn_mgnt, button_index);
    return insert_multi_click_event(stat, target_count, valid_level);
}


SYSTEM_ERROR_CODE_E unregister_multi_click_event(btn_event_mgnt_t *btn_mgnt, uint8_t button_index, uint8_t target_count) {
    button_statistic_t *stat = GET_BTN_STAT(btn_mgnt, button_index);
    return delete_multi_click_event(stat, target_count);
}


void button_state_update(btn_event_mgnt_t *btn_mgnt, uint8_t button_index, BUTTON_TRIGGER_LEVEL_E trigger_level) {
    button_statistic_t *stat = GET_BTN_STAT(btn_mgnt, button_index);
    bool trigger = btn_mgnt->get_btn_level_handler(button_index);
    if(BUTTON_TRIGGER_LEVEL_LOW == trigger_level) {
        trigger = !trigger;
    }
	
    if(trigger) {
		/* Pretent to press overflow */
		if(KEY_PRESS_OVERFLOW_COUNT >= stat->press_count) {
			stat->press_count ++;
		}
    } 
    else {
        stat->startup_press_valid = false;
    }
    
	if(stat->is_startup) {
		stat->is_startup = false;
		stat->startup_press_valid = trigger ? true : false;
	}
	
	if(stat->startup_press_valid) {
        /* Startup continuous press */
        if(KEY_STARTUP_SHORT_PRESS_THRES_COUNT == stat->press_count) {
            /* Startup short press */
            auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_STARTUP_SHORT_PRESS);
        } else if(KEY_STARTUP_LONG_PRESS_THRES_COUNT == stat->press_count) {
            /* Startup short press */
            auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_STARTUP_LONG_PRESS);
            stat->startup_press_valid = false;
        }
	} 
    else {
        /* Normal continuous press */
        if(KEY_SHORT_PRESS_THRES_COUNT == stat->press_count) {
            /* Short press begin */
            auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_SHORT_PRESS);
        } else if(KEY_MIDDLE_PRESS_THRES_COUNT == stat->press_count) {
            /* Middle press begin */
            auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_MIDDLE_PRESS);
        } else if(KEY_LONG_PRESS_THRES_COUNT == stat->press_count) {
            /* Long press begin */
            auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_LONG_PRESS);
        }
    }
    
    /* Just released */
    if(false == trigger && stat->last_trigger) {
        auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_RELEASE);
        /*  */
        if(stat->press_count >= KEY_SINGLE_PRESS_THRES_COUNT && stat->press_count < KEY_SHORT_PRESS_THRES_COUNT) {
            uint32_t current_tick = xTaskGetTickCount();
            uint32_t release_interval = current_tick - stat->last_release_tick;
            stat->last_release_tick = current_tick;
            /* (Re)arm the click-decision window. Single/double/multi click is NOT
             * reported immediately on release: the whole click sequence is only
             * resolved once this window expires, so leading clicks of a multi-click
             * sequence can never fire a premature SINGLE_CLICK. */
            stat->multi_click_valid_count = KEY_MULTI_CLICK_VALID_COUNT;
            if(release_interval <= KEY_MULTI_CLICK_WINDOW_MS) {
                stat->consecutive_clicks ++;
            } else {
                stat->consecutive_clicks = 1;
            }
        }
        /* Long press : Must release until 1.5s later */
        else if(stat->press_count >= KEY_LONG_PRESS_THRES_COUNT && stat->press_count < (KEY_LONG_PRESS_THRES_COUNT + KEY_PRESS_VALID_RELEASE_THRES_COUNT)) {
            auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_LONG_PRESS_RELEASE);
        }
        /* Middle press : Must release until 1.5s later */
        else if(stat->press_count >= KEY_MIDDLE_PRESS_THRES_COUNT && stat->press_count < (KEY_MIDDLE_PRESS_THRES_COUNT + KEY_PRESS_VALID_RELEASE_THRES_COUNT)) {
            auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_MIDDLE_PRESS_RELEASE);
        }
        /* Short press : Must release until 1.5s later */
        else if(stat->press_count >= KEY_SHORT_PRESS_THRES_COUNT && stat->press_count < (KEY_SHORT_PRESS_THRES_COUNT + KEY_PRESS_VALID_RELEASE_THRES_COUNT)) {
            auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_SHORT_PRESS_RELEASE);
        }
        stat->press_count = 0;
    } 
    /* Just triggered */
    else if(trigger && false == stat->last_trigger) {
        auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_TRIGGER);
    }
    /* Click decision scan : a pending click sequence is resolved exactly ONCE when
     * its window expires. The whole sequence yields a single event so multi-click
     * sequences never report single clicks before they are complete. */
    if(stat->multi_click_valid_count > 0) {
        stat->multi_click_valid_count --;
        if(stat->multi_click_valid_count == 0) {
            xSemaphoreTake(stat->handle_mutex, portMAX_DELAY);
            uint8_t total_clicks = stat->consecutive_clicks;
            if(total_clicks >= 3) {
                /* Registered multi-click : report once only when an exact match
                 * exists. An unmatched count stays silent on purpose so that a
                 * (mis)counted sequence never falls back to single clicks. */
                int list_len = get_list_length(stat->multi_conf_list);
                iterator_t it = begin_of_list(stat->multi_conf_list);
                for(int i = 0; i < list_len; i ++) {
                    multi_config_t *_cfg = get_list_data(it);
                    if(NULL != _cfg && _cfg->target_count == total_clicks) {
                        uint32_t event_data = _cfg->target_count + BUTTON_MULTI_CLICK;
                        auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)event_data);
                        break;
                    }
                    it = get_list_next(it);
                }
            } else if(total_clicks == 2) {
                /* Double click */
                auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_DOUBLE_CLICK);
            } else if(total_clicks == 1) {
                /* Single click */
                auto_event_run_handlers(btn_mgnt->btn_event_handler[button_index], (void *)BUTTON_SINGLE_CLICK);
            }
            /* Reset decision state for the next sequence */
            stat->consecutive_clicks = 0;
            stat->curr_multi_event_item = end_of_list(stat->multi_conf_list);
            xSemaphoreGive(stat->handle_mutex);
        }
    }
    
    stat->last_trigger = trigger;
}


void reset_button_startup_state(btn_event_mgnt_t *btn_mgnt, uint8_t button_index) {
    button_statistic_t *stat = GET_BTN_STAT(btn_mgnt, button_index);
    stat->is_startup = true;
}


SYSTEM_ERROR_CODE_E button_service_init(btn_event_mgnt_t *btn_mgnt) {
    if(NULL == btn_mgnt) {
        return ERR_INVALID_POINTER;
    }
    if(NULL == btn_mgnt->get_btn_level_handler) {
        return ERR_INVALID_POINTER;
    }
    if(NULL != btn_mgnt->btn_event_handler || NULL != btn_mgnt->btn_statis) {
        return ERR_REINIT;
    }
	if(0 == btn_mgnt->btn_count) {
		return ERR_INVALID_ARG;
	}
    btn_mgnt->btn_statis = s_calloc(btn_mgnt->btn_count, sizeof(button_statistic_t));
    btn_mgnt->btn_event_handler = s_calloc(btn_mgnt->btn_count, sizeof(auto_handler));
    if(NULL == btn_mgnt->btn_statis || NULL == btn_mgnt->btn_event_handler) {
        goto ALLOC_FAIL;
    }
    memset(btn_mgnt->btn_statis, 0, btn_mgnt->btn_count * sizeof(button_statistic_t));
    memset(btn_mgnt->btn_event_handler, 0, btn_mgnt->btn_count * sizeof(auto_handler));
    for(uint8_t i = 0; i < btn_mgnt->btn_count; i ++) {
        auto_event_manager_init(&btn_mgnt->btn_event_handler[i], BUTTON_HANDLER_CAPACITY);
        button_statistic_t *stat = GET_BTN_STAT(btn_mgnt, i);
        stat->multi_event_max_count = BUTTON_MULTI_CLICK_EVENT_CAPACITY;
        stat->handle_mutex = xSemaphoreCreateMutex();
        if(NULL == stat->handle_mutex) {
            goto ALLOC_FAIL;
        }
        xSemaphoreTake(stat->handle_mutex, portMAX_DELAY);
        SYSTEM_ERROR_CODE_E ret = init_list(&stat->multi_conf_list, sizeof(multi_config_t), s_calloc, s_free);
        xSemaphoreGive(stat->handle_mutex);
        if(ret != ERR_NONE) {
            goto ALLOC_FAIL;
        }
        stat->curr_multi_event_item = end_of_list(stat->multi_conf_list);
		stat->is_startup = true;
    }
    
    return ERR_NONE;
    
ALLOC_FAIL:
    for(uint8_t i = 0; i < btn_mgnt->btn_count; i ++) {
        button_statistic_t *stat = GET_BTN_STAT(btn_mgnt, i);
        if(NULL != stat->handle_mutex) {
            vSemaphoreDelete(stat->handle_mutex);
        }
        if(NULL != stat->multi_conf_list) {
            destroy_list(&stat->multi_conf_list);
        }
    }
    if(btn_mgnt->btn_statis) {
        s_free(btn_mgnt->btn_statis);
    }
    if(btn_mgnt->btn_event_handler) {
        s_free(btn_mgnt->btn_event_handler);
    }
    return ERR_NO_MEM;
}

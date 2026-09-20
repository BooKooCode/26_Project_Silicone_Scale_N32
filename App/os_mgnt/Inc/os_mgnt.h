#ifndef __OS_MGNT_H__
#define __OS_MGNT_H__

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "bookoo_error_def.h"
#include <stdbool.h>


typedef enum {
	OS_TASK_REGULAR = 0,
    OS_TASK_STORE,
    OS_TASK_HCI,
	OS_TASK_BAT,
	OS_TASK_DISP,
	OS_TASK_MASS,
	OS_TASK_FSM,
	OS_TASK_FSMREQ,
	OS_TASK_TIMREQ,
    OS_TASK_DEBUG,
	
	OS_TASK_TOTAL_COUNT
} OS_TASK_IDNEX_E;


typedef struct {
    const char *name;
    TaskFunction_t task_code;
    uint32_t priority;
    void *paramaters;
    uint32_t stack_size;
	TaskHandle_t handler;
	TickType_t wake_tick;
	size_t free_stack;
    xQueueHandle task_queue;
} os_task_info_t;

extern volatile uint32_t g_tickless_stop2_entry_count;


void os_mgnt_init(void);
void os_mgnt_set_stop_block(uint32_t blocker, bool blocked);


SYSTEM_ERROR_CODE_E send_queue_belong_task(OS_TASK_IDNEX_E task_index, void *req);
SYSTEM_ERROR_CODE_E send_fsm_critical_request(uint8_t request);
bool take_fsm_critical_request(uint8_t *request);


#endif

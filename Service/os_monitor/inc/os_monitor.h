#ifndef __OS_MONITOR_H__
#define __OS_MONITOR_H__

#include "bookoo_error_def.h"
#include "os_monitor_conf.h"


typedef enum {
    OS_IDLE_REQ_NONE = 0,
    OS_IDLE_REQ_SYS_RESET = 1,
} OS_IDLE_REQ_E;

/* configuration moved to os_monitor_conf.h */

typedef uint16_t os_index_t;


SYSTEM_ERROR_CODE_E trans_os_idle_req(uint8_t req, uint32_t timeout);

SYSTEM_ERROR_CODE_E append_os_record(os_index_t *_index, void *_os_handler, uint32_t _alive_ms);

SYSTEM_ERROR_CODE_E update_os_thread_state(os_index_t _index);

SYSTEM_ERROR_CODE_E record_os_exit(os_index_t _index);

SYSTEM_ERROR_CODE_E os_mgnt_start_auto_check(void);

SYSTEM_ERROR_CODE_E os_mgnt_init(void);

SYSTEM_ERROR_CODE_E append_queue_record(os_index_t *_index, void *_q_handler, uint32_t _capacity);


#endif

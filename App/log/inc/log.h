#ifndef __LOG_H__
#define __LOG_H__

#include "bookoo_error_def.h"


#define LOG_TS_PRECISION_S                  0
#define LOG_TS_PRECISION_MS                 1
#define LOG_TS_PRECISION_US                 2

typedef enum {
    LOG_SEVERITY_NONE = 0,
    LOG_SEVERITY_ERROR,
    LOG_SEVERITY_WARNING,
    LOG_SEVERITY_INFO,
    LOG_SEVERITY_DEBUG,
    LOG_SEVERITY_RAW_INFO,
    LOG_SEVERITY_CLC
} LOG_SEVERITY_E;


typedef int (*log_rx_process_handle_t)(void*);


SYSTEM_ERROR_CODE_E log_push(LOG_SEVERITY_E severity, const char* format, ...);


#define LOG_ERROR(tag, log...)      log_push(LOG_SEVERITY_ERROR, "[" tag "]:" log)
#define LOG_WARNING(tag, log...)    log_push(LOG_SEVERITY_WARNING, "[" tag "]:" log)
#define LOG_INFO(tag, log...)       log_push(LOG_SEVERITY_INFO, "[" tag "]:" log)
#define LOG_DEBUG(tag, log...)      log_push(LOG_SEVERITY_DEBUG, "[" tag "]:" log)
#define LOG_RAW_INFO(tag, log...)   log_push(LOG_SEVERITY_RAW_INFO, "[" tag "]:" log)
#define LOG_CLC(tag, log...)        log_push(LOG_SEVERITY_CLC, "[" tag "]:" log)


SYSTEM_ERROR_CODE_E register_log_rx_event(log_rx_process_handle_t _handle);

SYSTEM_ERROR_CODE_E unregister_log_rx_event(log_rx_process_handle_t _handle);

SYSTEM_ERROR_CODE_E log_init(void);


#endif


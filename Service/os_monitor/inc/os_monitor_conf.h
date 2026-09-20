#ifndef __OS_MONITOR_CONF_H__
#define __OS_MONITOR_CONF_H__

#ifndef OS_CHECK_PERIOD
    #define OS_CHECK_PERIOD                     100U
#endif

#ifndef OS_TASK_MAX_COUNT
    #define OS_TASK_MAX_COUNT                   32U
#endif

#ifndef OS_QUEUE_MAX_COUNT
    #define OS_QUEUE_MAX_COUNT                  16U
#endif

#ifndef OS_QUEUE_NAME_MAX_LEN
    #define OS_QUEUE_NAME_MAX_LEN               24U
#endif

#ifndef OS_QUEUE_MONITOR_MIN_CAPACITY
    #define OS_QUEUE_MONITOR_MIN_CAPACITY       4U
#endif

#ifndef OS_QUEUE_OVERFLOW_THRESHOLD_MIN_COUNT
    #define OS_QUEUE_OVERFLOW_THRESHOLD_MIN_COUNT       0U
#endif

#ifndef OS_QUEUE_OVERFLOW_THRESHOLD_PERCENT
    #define OS_QUEUE_OVERFLOW_THRESHOLD_PERCENT         100U
#endif

#ifndef OS_MGNT_REQ_QUEUE_SIZE
    #define OS_MGNT_REQ_QUEUE_SIZE              2U
#endif

#endif


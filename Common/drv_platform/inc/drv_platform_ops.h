#ifndef __DRV_PLATFORM_OPS_H__
#define __DRV_PLATFORM_OPS_H__

/**
 * @file drv_platform_ops.h
 * @brief Generic platform operations interface for device drivers.
 *
 * Provides a reusable platform operations structure for device drivers
 * that require dynamic memory allocation, mutual exclusion, and hardware
 * read/write access. Each driver module can typedef or use this structure
 * directly, avoiding redundant function pointer declarations.
 *
 * Dependency level: only depends on <stdint.h>, <stddef.h>, and
 * system_error_def.h - the same level as existing driver dependencies,
 * introducing no new dependency layers.
 *
 * hw_write / hw_read use a "mem-addr" style signature:
 *   dev_addr  - I2C/SPI device address
 *   mem_addr  - register address or command byte (pass 0 for stream-only devices)
 *   data      - data buffer
 *   size      - number of bytes
 */

#include <stddef.h>
#include <stdint.h>
#include "system_error_def.h"

/* ---------- Function pointer types ---------- */

/** Memory allocation (calloc semantics: nmemb elements of size bytes, zero-initialised) */
typedef void *(*drv_calloc_h)(size_t nmemb, size_t size);

/** Memory deallocation */
typedef void (*drv_free_h)(void *ptr);

/** Acquire mutex; returns 0 on success */
typedef int (*drv_lock_h)(void *lock_data);

/** Release mutex; returns 0 on success */
typedef int (*drv_unlock_h)(void *lock_data);

/** Hardware write (mem-addr style) */
typedef SYSTEM_ERROR_CODE_E (*drv_hw_write_h)(uint16_t dev_addr,
                                               uint16_t mem_addr,
                                               uint8_t *data,
                                               uint16_t size);

/** Hardware read (mem-addr style) */
typedef SYSTEM_ERROR_CODE_E (*drv_hw_read_h)(uint16_t dev_addr,
                                              uint16_t mem_addr,
                                              uint8_t *data,
                                              uint16_t size);

/** Blocking delay in milliseconds */
typedef void (*drv_delay_ms_h)(uint32_t ms);

/** Optional: Get current time in milliseconds (e.g. for timeout handling) */
typedef uint32_t (*drv_get_time_ms_h)(void); 

/* ---------- Generic platform operations structure ---------- */

typedef struct {
    drv_calloc_h  calloc;    /*!< Memory allocation callback */
    drv_free_h    free;      /*!< Memory deallocation callback */
} drv_mem_ops_t;


typedef struct {
    drv_lock_h    lock;      /*!< Lock acquisition callback */
    drv_unlock_h  unlock;    /*!< Lock release callback */
    void *lock_data;         /*!< Opaque data passed to lock/unlock (e.g. mutex handle) */
} drv_locker_ops_t;


typedef struct {
    drv_hw_write_h hw_write; /*!< Hardware write callback */
    drv_hw_read_h  hw_read;  /*!< Hardware read callback */
} drv_hw_ops_t;


typedef struct {
    drv_delay_ms_h delay_ms; /*!< Blocking millisecond delay callback */
    drv_get_time_ms_h get_time_ms; /*!< Optional: get current time in ms for timeout handling */
} drv_ts_ops_t;


typedef struct {
    drv_calloc_h  calloc;    /*!< Memory allocation callback */
    drv_free_h    free;      /*!< Memory deallocation callback */
    drv_lock_h    lock;      /*!< Lock acquisition callback */
    drv_unlock_h  unlock;    /*!< Lock release callback */
    void *lock_data;         /*!< Opaque data passed to lock/unlock (e.g. mutex handle) */
} drv_mem_locker_ops_t;


typedef struct {
    drv_calloc_h  calloc;    /*!< Memory allocation callback */
    drv_free_h    free;      /*!< Memory deallocation callback */
    drv_lock_h    lock;      /*!< Lock acquisition callback */
    drv_unlock_h  unlock;    /*!< Lock release callback */
    drv_hw_write_h hw_write; /*!< Hardware write callback */
    drv_hw_read_h  hw_read;  /*!< Hardware read callback */
    drv_delay_ms_h delay_ms; /*!< Blocking millisecond delay callback */
    drv_get_time_ms_h get_time_ms; /*!< Optional: get current time in ms for timeout handling */
    void *lock_data;         /*!< Opaque data passed to lock/unlock (e.g. mutex handle) */
} drv_platform_ops_t;

#endif /* __DRV_PLATFORM_OPS_H__ */

#ifndef APP_ERROR_H__
#define APP_ERROR_H__

#include <stdint.h>
#include "bookoo_error_def.h"

#ifdef __cplusplus
extern "C" {
#endif

void Error_Handler(void);

typedef struct {
    uint32_t magic;
    uint32_t sequence;
    uint32_t error_code;
    uint32_t caller_pc;
    uint32_t ipsr;
    uint32_t fault_sp;
    uint32_t fault_r0;
    uint32_t fault_r1;
    uint32_t fault_r2;
    uint32_t fault_r3;
    uint32_t fault_r12;
    uint32_t fault_lr;
    uint32_t fault_pc;
    uint32_t fault_xpsr;
    uint32_t fault_cfsr;
    uint32_t fault_hfsr;
    const char *file;
    uint32_t line;
    char task_name[16];
} app_error_trace_info_t;

extern volatile app_error_trace_info_t g_app_error_trace_last;

void app_error_trace_record(uint32_t error_code, uint32_t caller_pc, const char *file, uint32_t line);
void app_error_trace_record_if_empty(uint32_t error_code, uint32_t caller_pc, const char *file, uint32_t line);
void app_error_trace_record_from_error_handler(void);
void app_error_trace_record_hardfault_frame(uint32_t *stack_frame);
void app_error_trace_set_task_name(const char *task_name);

#if defined(__GNUC__) || defined(__clang__)
#define APP_ERROR_CALLSITE_PC() ((uint32_t)(uintptr_t)__builtin_return_address(0))
#else
#define APP_ERROR_CALLSITE_PC() (0U)
#endif

static inline void app_error_handler_bare(uint32_t error_code, uint32_t caller_pc, const char *file, uint32_t line)
{
    app_error_trace_record(error_code, caller_pc, file, line);
    Error_Handler();
}

#ifndef APP_ERROR_HANDLER
#define APP_ERROR_HANDLER(ERROR_CODE) \
    app_error_handler_bare((uint32_t)(ERROR_CODE), APP_ERROR_CALLSITE_PC(), __FILE__, (uint32_t)__LINE__)
#endif

#ifndef APP_ERROR_CHECK
#define APP_ERROR_CHECK(ERR_CODE) do { if ((uint32_t)(ERR_CODE) != 0U) { APP_ERROR_HANDLER(ERR_CODE); } } while (0)
#endif

#ifndef ASSERT
#define ASSERT(EXPR) do { if (!(EXPR)) { APP_ERROR_HANDLER(BOOKOO_ERROR_HARDFAULT); } } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif
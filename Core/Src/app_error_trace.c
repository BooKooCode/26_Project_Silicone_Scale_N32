#include "app_error.h"
#include <stddef.h>
#include "main.h"

#define APP_ERROR_TRACE_MAGIC (0x45525230UL)

volatile app_error_trace_info_t g_app_error_trace_last = {0};

static uint32_t app_error_normalize_pc(uint32_t pc)
{
    return pc & (~1UL);
}

void app_error_trace_record(uint32_t error_code, uint32_t caller_pc, const char *file, uint32_t line)
{
    g_app_error_trace_last.magic = APP_ERROR_TRACE_MAGIC;
    g_app_error_trace_last.sequence++;
    g_app_error_trace_last.error_code = error_code;
    g_app_error_trace_last.caller_pc = app_error_normalize_pc(caller_pc);
    g_app_error_trace_last.ipsr = __get_IPSR();
    g_app_error_trace_last.file = file;
    g_app_error_trace_last.line = line;
}

void app_error_trace_record_if_empty(uint32_t error_code, uint32_t caller_pc, const char *file, uint32_t line)
{
    if (g_app_error_trace_last.magic != APP_ERROR_TRACE_MAGIC) {
        app_error_trace_record(error_code, caller_pc, file, line);
    }
}

void app_error_trace_record_hardfault_frame(uint32_t *stack_frame)
{
    extern uint32_t _estack;
    uint32_t caller_pc = 0U;
    uintptr_t frame_address = (uintptr_t)stack_frame;

    if ((frame_address < 0x20000000UL) ||
        (frame_address > ((uintptr_t)&_estack - (8U * sizeof(uint32_t)))) ||
        ((frame_address & 3U) != 0U)) {
        stack_frame = NULL;
    }

    if (stack_frame != NULL) {
        caller_pc = stack_frame[6];
    }
    app_error_trace_record_if_empty(BOOKOO_ERROR_HARDFAULT, caller_pc, "HardFault_Handler", 0U);

    g_app_error_trace_last.fault_cfsr = SCB->CFSR;
    g_app_error_trace_last.fault_hfsr = SCB->HFSR;
    if (stack_frame == NULL) {
        g_app_error_trace_last.fault_sp = 0U;
        g_app_error_trace_last.fault_r0 = 0U;
        g_app_error_trace_last.fault_r1 = 0U;
        g_app_error_trace_last.fault_r2 = 0U;
        g_app_error_trace_last.fault_r3 = 0U;
        g_app_error_trace_last.fault_r12 = 0U;
        g_app_error_trace_last.fault_lr = 0U;
        g_app_error_trace_last.fault_pc = 0U;
        g_app_error_trace_last.fault_xpsr = 0U;
        return;
    }

    g_app_error_trace_last.fault_sp = (uint32_t)(uintptr_t)stack_frame;
    g_app_error_trace_last.fault_r0 = stack_frame[0];
    g_app_error_trace_last.fault_r1 = stack_frame[1];
    g_app_error_trace_last.fault_r2 = stack_frame[2];
    g_app_error_trace_last.fault_r3 = stack_frame[3];
    g_app_error_trace_last.fault_r12 = stack_frame[4];
    g_app_error_trace_last.fault_lr = stack_frame[5];
    g_app_error_trace_last.fault_pc = app_error_normalize_pc(stack_frame[6]);
    g_app_error_trace_last.fault_xpsr = stack_frame[7];
}

void app_error_trace_record_from_error_handler(void)
{
    uint32_t caller_pc = 0U;
#if defined(__GNUC__) || defined(__clang__)
    caller_pc = (uint32_t)(uintptr_t)__builtin_return_address(0);
#endif
    app_error_trace_record_if_empty(BOOKOO_ERROR_HARDFAULT, caller_pc, "Error_Handler", 0U);
}

__attribute__((noreturn)) void HardFault_Handler_C(uint32_t *stack_frame)
{
    __disable_irq();
    app_error_trace_record_hardfault_frame(stack_frame);
    for (;;) {
        __NOP();
    }
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "tst lr, #16\n"
        "it eq\n"
        "addeq r0, r0, #72\n"
        "b HardFault_Handler_C\n"
    );
}

void app_error_trace_set_task_name(const char *task_name)
{
    size_t index = 0U;

    if (task_name == NULL) {
        g_app_error_trace_last.task_name[0] = '\0';
        return;
    }
    for (index = 0U; index < (sizeof(g_app_error_trace_last.task_name) - 1U); index++) {
        if (task_name[index] == '\0') {
            break;
        }
        g_app_error_trace_last.task_name[index] = task_name[index];
    }
    g_app_error_trace_last.task_name[index] = '\0';
}
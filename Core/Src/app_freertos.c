#include "app_freertos.h"
#include "app_error.h"

#define appIDLE_STACK_SIZE_WORDS 288U

osEventFlagsId_t os_init_eventHandle;
osThreadId_t defaultTaskHandle;

static const osThreadAttr_t defaultTask_attributes = {
    .name = "defaultTask",
    .priority = osPriorityNormal,
    .stack_size = 128U * 4U
};

static void StartDefaultTask(void *argument)
{
    (void)argument;
    vTaskSuspend(defaultTaskHandle);
    for (;;) {
        osDelay(1U);
    }
}

void NS_FREERTOS_Init(void)
{
    defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);
    if (defaultTaskHandle == NULL) {
        Error_Handler();
    }

    os_init_eventHandle = osEventFlagsNew(NULL);
    if (os_init_eventHandle == NULL) {
        Error_Handler();
    }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **task_buffer,
                                   StackType_t **stack_buffer,
                                   uint32_t *stack_size)
{
    static StaticTask_t idle_task;
    static StackType_t idle_stack[appIDLE_STACK_SIZE_WORDS];

    *task_buffer = &idle_task;
    *stack_buffer = idle_stack;
    *stack_size = appIDLE_STACK_SIZE_WORDS;
}

void app_rtos_assert_failed(const char *file, int line)
{
    __disable_irq();
    app_error_trace_record(BOOKOO_ERROR_HARDFAULT, APP_ERROR_CALLSITE_PC(), file, (uint32_t)line);
    Error_Handler();
}
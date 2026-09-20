/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "app_error.h"
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "dev_status.h"
#include "dev_config.h"
#include "log.h"
#include "main.h"
#include "rtc.h"
#include "n32l40x_pwr.h"

#include "os_mgnt.h"
#include "dev_buzzer.h"
#include "scale_mass_mgnt.h"


/* Defines -------------------------------------------------------------------*/
#ifndef RTOS_HEAP_LOW_WATERMARK_BYTES
#define RTOS_HEAP_LOW_WATERMARK_BYTES    (64U)
#endif

#ifndef RTOS_TICKLESS_STOP_THRESHOLD_TICKS
#define RTOS_TICKLESS_STOP_THRESHOLD_TICKS    (5U)
#endif

#define RTOS_RTC_WAKEUP_CLOCK_HZ              RTC_WAKEUP_CLOCK_HZ
#define RTOS_RTC_WAKEUP_COUNTER_MAX           RTC_WAKEUP_COUNTER_MAX


/* Private variables ---------------------------------------------------------*/

/* Private function declarations ---------------------------------------------*/
void regularity_task(void * arg);
void store_task(void *argument);
void hci_task(void *argument);
void bat_task(void * arg);
void disp_task(void * arg);
void mass_task(void * arg);
void FSM_task(void * arg);
void FSMreq_task(void * arg);
void timingreq_task(void * arg);
void debug_task(void *argument);

os_task_info_t os_task_info[OS_TASK_TOTAL_COUNT] = {
    {.name = "App.Regular",     .task_code = regularity_task,   .priority = 1,  .stack_size = 288,  .paramaters = os_task_info},    /* OS_TASK_REGULAR */
    {.name = "App.Store",       .task_code = store_task,        .priority = 2,  .stack_size = 256,  .paramaters = os_task_info},    /* OS_TASK_STORE */
    {.name = "App.Hci",         .task_code = hci_task,          .priority = 2,  .stack_size = 192,  .paramaters = os_task_info},    /* OS_TASK_HCI */
    {.name = "App.Bat",         .task_code = bat_task,          .priority = 1,  .stack_size = 128,  .paramaters = os_task_info},    /* OS_TASK_BAT */
    {.name = "App.Disp",        .task_code = disp_task,         .priority = 2,  .stack_size = 128,  .paramaters = os_task_info},    /* OS_TASK_DISP */
    {.name = "App.Mass",        .task_code = mass_task,         .priority = 3,  .stack_size = 192,  .paramaters = os_task_info},    /* OS_TASK_MASS */
    {.name = "App.FSM",         .task_code = FSM_task,          .priority = 3,  .stack_size = 256,  .paramaters = os_task_info},    /* OS_TASK_FSM */
    {.name = "App.FSMreq",      .task_code = FSMreq_task,       .priority = 5,  .stack_size = 192,  .paramaters = os_task_info},    /* OS_TASK_FSMREQ */
    {.name = "App.timreq",      .task_code = timingreq_task,    .priority = 4,  .stack_size = 80,   .paramaters = os_task_info},    /* OS_TASK_TIMREQ */
    {.name = "App.Debug",       .task_code = debug_task,        .priority = 2,  .stack_size = 96,   .paramaters = os_task_info},    /* OS_TASK_DEBUG */
};


size_t rtos_free_heap = 0;
size_t min_ever_free_heap = 0;
char overflow_task_name[configMAX_TASK_NAME_LEN] = {0};
char malloc_fail_task_name[configMAX_TASK_NAME_LEN] = {0};

static uint32_t s_rtcSubTicksBeforeSleep = 0;
static bool s_rtcSnapshotValid = false;
static TickType_t s_completedIdleTicks = 0;
static bool s_rtcWakeupArmed = false;
static bool s_stopModeEntered = false;
static volatile uint32_t s_fsm_critical_requests = 0U;
static volatile uint32_t s_tickless_stop_blockers = 0U;

/* Set to true via debugger (or init code) to force WFI-only sleep,
 * bypassing STOP mode entirely.  Useful for isolating STOP-related faults. */
volatile bool g_tickless_stop_disabled = false; /* set true to force WFI-only */
volatile uint32_t g_tickless_stop2_entry_count = 0U;


void os_mgnt_set_stop_block(uint32_t blocker, bool blocked)
{
    taskENTER_CRITICAL();
    if(blocked) {
        s_tickless_stop_blockers |= blocker;
    } else {
        s_tickless_stop_blockers &= ~blocker;
    }
    taskEXIT_CRITICAL();
}

static uint32_t os_mgnt_rtc_subticks_per_second(void)
{
    return RTC_SUBTICKS_PER_SECOND;
}

static TickType_t os_mgnt_elapsed_idle_ticks(uint32_t beforeSubTicks, uint32_t afterSubTicks)
{
    uint32_t subTicksPerSecond = os_mgnt_rtc_subticks_per_second();
    uint32_t subTicksPerDay = 24U * 60U * 60U * subTicksPerSecond;
    uint32_t elapsedSubTicks = 0;
    uint64_t elapsedTicks = 0;

    if(afterSubTicks >= beforeSubTicks) {
        elapsedSubTicks = afterSubTicks - beforeSubTicks;
    } else {
        elapsedSubTicks = (subTicksPerDay - beforeSubTicks) + afterSubTicks;
    }

    elapsedTicks = (uint64_t)elapsedSubTicks * (uint64_t)configTICK_RATE_HZ;
    elapsedTicks /= subTicksPerSecond;

    if(elapsedTicks > (uint64_t)portMAX_DELAY) {
        elapsedTicks = (uint64_t)portMAX_DELAY;
    }

    return (TickType_t)elapsedTicks;
}

static TickType_t os_mgnt_limit_idle_ticks(TickType_t expectedIdleTicks)
{
    uint64_t maxIdleTicks = ((uint64_t)RTOS_RTC_WAKEUP_COUNTER_MAX * (uint64_t)configTICK_RATE_HZ) / RTOS_RTC_WAKEUP_CLOCK_HZ;

    if(maxIdleTicks == 0U) {
        return expectedIdleTicks;
    }

    if((uint64_t)expectedIdleTicks > maxIdleTicks) {
        return (TickType_t)maxIdleTicks;
    }

    return expectedIdleTicks;
}

static uint32_t os_mgnt_wakeup_counter_from_idle_ticks(TickType_t expectedIdleTicks)
{
    uint64_t wakeupCounts = ((uint64_t)expectedIdleTicks * RTOS_RTC_WAKEUP_CLOCK_HZ) + (configTICK_RATE_HZ - 1U);

    wakeupCounts /= configTICK_RATE_HZ;
    if(wakeupCounts == 0U) {
        wakeupCounts = 1U;
    } else if(wakeupCounts > RTOS_RTC_WAKEUP_COUNTER_MAX) {
        wakeupCounts = RTOS_RTC_WAKEUP_COUNTER_MAX;
    }

    return (uint32_t)(wakeupCounts - 1U);
}

static bool os_mgnt_arm_rtc_wakeup(uint32_t wakeupCounter)
{
    return rtc_arm_wakeup(wakeupCounter);
}

static bool os_mgnt_deactivate_rtc_wakeup(void)
{
    return rtc_disarm_wakeup();
}

/* Function prototypes -------------------------------------------------------*/
/**
* @brief  Initialization of device management service
* @param  None.
* @return None.
*/

void vApplicationIdleHook(void) {
    static rtc_status_t last_rtc_status = RTC_STATUS_READY;
    rtc_status_t current_rtc_status = rtc_get_status();
    if(current_rtc_status != last_rtc_status) {
        last_rtc_status = current_rtc_status;
        if(current_rtc_status != RTC_STATUS_READY) {
            LOG_ERROR("os_mgnt", "RTC unavailable: status=%u, deep sleep blocked", (unsigned int)current_rtc_status);
        }
    }
	rtos_free_heap = xPortGetFreeHeapSize();
	min_ever_free_heap = xPortGetMinimumEverFreeHeapSize();
    static bool s_heap_low_reported = false;
    if((rtos_free_heap <= RTOS_HEAP_LOW_WATERMARK_BYTES) ||
       (min_ever_free_heap <= RTOS_HEAP_LOW_WATERMARK_BYTES)) {
        if(!s_heap_low_reported) {
            s_heap_low_reported = true;
            LOG_ERROR("os_mgnt", "RTOS heap is low: free=%u, min=%u", (unsigned int)rtos_free_heap, (unsigned int)min_ever_free_heap);
        }
    }
    if(uxTaskPriorityGet(NULL) != tskIDLE_PRIORITY) {
		APP_ERROR_HANDLER(BOOKOO_ERROR_THREADHEAPFAULT);
		/* �����������Ʋ����κ�ʱ����Ч���������ƻ���IDLE������ƿ��е����ȼ�ʱ�Ż���Ч */
	}
}


void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
	memset(overflow_task_name, 0, configMAX_TASK_NAME_LEN);
	strcpy(overflow_task_name, pcTaskName);
    app_error_trace_set_task_name(pcTaskName);
    APP_ERROR_HANDLER(BOOKOO_ERROR_THREADHEAPFAULT);
    LOG_ERROR("os_mgnt", "RTOS overflow, task name: %s", overflow_task_name);
}


void vApplicationMallocFailedHook(void) {
    const char *name = pcTaskGetName(NULL);
    memset(malloc_fail_task_name, 0, configMAX_TASK_NAME_LEN);
	strcpy(malloc_fail_task_name, name);
    LOG_ERROR("os_mgnt", "RTOS malloc fail, task name: %s", malloc_fail_task_name);
}

void PreSleepProcessing(uint32_t ulExpectedIdleTime) {
    s_completedIdleTicks = 0;
    s_stopModeEntered = false;
    s_rtcWakeupArmed = false;
    s_rtcSnapshotValid = false;

    if(ulExpectedIdleTime == 0U) {
        return;
    }
    s_rtcSnapshotValid = rtc_capture_subticks(&s_rtcSubTicksBeforeSleep);
    if(!s_rtcSnapshotValid) {
        return;
    }

    PWR_ClearFlag(PWR_WKUP0_FLAG);
    PWR_ClearFlag(PWR_WKUP1_FLAG);
    PWR_ClearFlag(PWR_WKUP2_FLAG);

    if(os_mgnt_arm_rtc_wakeup(
           os_mgnt_wakeup_counter_from_idle_ticks((TickType_t)ulExpectedIdleTime))) {
        s_rtcWakeupArmed = true;
    }
}

void PostSleepProcessing(uint32_t ulExpectedIdleTime) {
    uint32_t rtcSubTicksAfterSleep = 0;

    if(s_stopModeEntered) {
        SystemClock_RestoreFromStop2();
        PWR_ClearFlag(PWR_WKUP0_FLAG);
        PWR_ClearFlag(PWR_WKUP1_FLAG);
        PWR_ClearFlag(PWR_WKUP2_FLAG);
    }

    bool afterSnapshotValid = s_rtcSnapshotValid && rtc_capture_subticks(&rtcSubTicksAfterSleep);
    if(s_rtcWakeupArmed) {
        (void)os_mgnt_deactivate_rtc_wakeup();
        s_rtcWakeupArmed = false;
    }

    if(!afterSnapshotValid) {
        s_completedIdleTicks = 0;
        return;
    }
    s_completedIdleTicks = os_mgnt_elapsed_idle_ticks(s_rtcSubTicksBeforeSleep, rtcSubTicksAfterSleep);
    if(s_completedIdleTicks > (TickType_t)ulExpectedIdleTime) {
        s_completedIdleTicks = (TickType_t)ulExpectedIdleTime;
    }
}

static __attribute__((noinline, section(".RamFunc"))) bool os_mgnt_enter_stop2_from_ram(void)
{
    uint32_t attempts = 1000000U;
    while ((PWR->STS2 & PWR_STS2_MRF) == 0U) {
        if (--attempts == 0U) {
            return false;
        }
    }
    PWR->CTRL3 = (PWR->CTRL3 & ~(uint32_t)PWR_CTRL3_RAMRETMASK) |
                 PWR_CTRL3_RAM1RET | PWR_CTRL3_RAM2RET;
    uint32_t previous_mode = PWR->CTRL1 & PWR_CTRL1_LPMSELMASK;
    PWR->CTRL1 = (PWR->CTRL1 & ~(uint32_t)PWR_CTRL1_LPMSELMASK) | PWR_CTRL1_STOP2;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __DSB();
    __ISB();
    __WFI();
    __DSB();
    __ISB();
    SCB->SCR &= ~(uint32_t)SCB_SCR_SLEEPDEEP_Msk;
    PWR->CTRL1 = (PWR->CTRL1 & ~(uint32_t)PWR_CTRL1_LPMSELMASK) | previous_mode;
    __DSB();
    __ISB();
    return true;
}

void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime ) {
    TickType_t clampedIdleTime = xExpectedIdleTime;

    if(clampedIdleTime == 0U) {
        return;
    }

    clampedIdleTime = os_mgnt_limit_idle_ticks(clampedIdleTime);

    __asm volatile( "cpsid i" ::: "memory" );
    __asm volatile( "dsb" );
    __asm volatile( "isb" );

    if( ( clampedIdleTime < RTOS_TICKLESS_STOP_THRESHOLD_TICKS )
        || !rtc_is_ready()
        || dev_buzzer_is_playing()
        || scale_mass_mgnt_stop_blocked()
        || (s_tickless_stop_blockers != 0U)
#if defined(DEBUG)
        /* Debug firmware keeps RTT/AHB accessible; Release retains STOP2. */
        || true
#endif
        || g_tickless_stop_disabled )
    {
        if(eTaskConfirmSleepModeStatus() == eAbortSleep) {
            __asm volatile( "cpsie i" ::: "memory" );
            return;
        }

        __asm volatile( "wfi" );
        __asm volatile( "cpsie i" ::: "memory" );
        return;
    }

    if(eTaskConfirmSleepModeStatus() == eAbortSleep) {
        __asm volatile( "cpsie i" ::: "memory" );
        return;
    }

    if((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U) {
        __enable_irq();
        return;
    }
    PreSleepProcessing((uint32_t)clampedIdleTime);
    if(!s_rtcWakeupArmed) {
        __asm volatile( "cpsie i" ::: "memory" );
        return;
    }

    uint32_t systick_control = SysTick->CTRL;
    SysTick->CTRL = systick_control & ~SysTick_CTRL_ENABLE_Msk;
    __DSB();
    if((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U) {
        (void)os_mgnt_deactivate_rtc_wakeup();
        s_rtcWakeupArmed = false;
        SysTick->CTRL = systick_control;
        __enable_irq();
        return;
    }
    if(!rtc_capture_subticks(&s_rtcSubTicksBeforeSleep)) {
        (void)os_mgnt_deactivate_rtc_wakeup();
        s_rtcWakeupArmed = false;
        SysTick->CTRL = systick_control;
        __enable_irq();
        return;
    }
    SystemClock_Config();

    /* Keep the first wake-up instructions in SRAM so STOP exit does not
     * immediately depend on Flash instruction fetch. */
    s_stopModeEntered = os_mgnt_enter_stop2_from_ram();
    if(s_stopModeEntered) {
        g_tickless_stop2_entry_count++;
    }

    PostSleepProcessing((uint32_t)clampedIdleTime);

    SysTick->LOAD = (SystemCoreClock / configTICK_RATE_HZ) - 1U;
    SysTick->VAL = 0U;

    if(s_completedIdleTicks > 0U) {
        vTaskStepTick(s_completedIdleTicks - 1U);
        SCB->ICSR = SCB_ICSR_PENDSTSET_Msk;
    }
    SysTick->CTRL = systick_control;

    __asm volatile( "cpsie i" ::: "memory" );
}


void os_mgnt_init(void) {
	BaseType_t res;
    
    for(uint8_t i = 0; i < OS_TASK_TOTAL_COUNT; i++) {
        if(os_task_info[i].name == NULL) {
            continue;
        }
        res = xTaskCreate(os_task_info[i].task_code, 
                            os_task_info[i].name, 
                            os_task_info[i].stack_size, 
                            os_task_info[i].paramaters, 
                            os_task_info[i].priority, 
                            &os_task_info[i].handler);
        if(res != pdPASS) {
            APP_ERROR_HANDLER(BOOKOO_ERROR_THREADHEAPFAULT);
        }
    }
}


SYSTEM_ERROR_CODE_E send_queue_belong_task(OS_TASK_IDNEX_E task_index, void *req) {
    if(NULL == req) {
        return ERR_INVALID_POINTER;
    }
    if(NULL == os_task_info[task_index].handler || NULL == os_task_info[task_index].task_queue) {
        return ERR_NOT_INIT;
    }
    BaseType_t _yield = pdFAIL;
    if(__get_IPSR()) {
        if(xQueueSendFromISR(os_task_info[task_index].task_queue, req, &_yield) != pdTRUE) {
            return ERR_FAIL;
        }
        portYIELD_FROM_ISR(_yield);
    }
    else {
        if(xQueueSend(os_task_info[task_index].task_queue, req, 0) != pdTRUE) {
            return ERR_FAIL;
        }
    }
    if(task_index == OS_TASK_FSMREQ) {
        if(__get_IPSR()) {
            vTaskNotifyGiveFromISR(os_task_info[task_index].handler, &_yield);
            portYIELD_FROM_ISR(_yield);
        }
        else {
            xTaskNotifyGive(os_task_info[task_index].handler);
        }
    }
    return ERR_NONE;
}

SYSTEM_ERROR_CODE_E send_fsm_critical_request(uint8_t request)
{
    BaseType_t yield = pdFALSE;

    if((request >= 32U) || (os_task_info[OS_TASK_FSMREQ].handler == NULL)) {
        return ERR_INVALID_ARG;
    }
    if(__get_IPSR()) {
        UBaseType_t saved_interrupt_status = taskENTER_CRITICAL_FROM_ISR();
        s_fsm_critical_requests |= (1UL << request);
        taskEXIT_CRITICAL_FROM_ISR(saved_interrupt_status);
        vTaskNotifyGiveFromISR(os_task_info[OS_TASK_FSMREQ].handler, &yield);
        portYIELD_FROM_ISR(yield);
    }
    else {
        taskENTER_CRITICAL();
        s_fsm_critical_requests |= (1UL << request);
        taskEXIT_CRITICAL();
        xTaskNotifyGive(os_task_info[OS_TASK_FSMREQ].handler);
    }
    return ERR_NONE;
}

bool take_fsm_critical_request(uint8_t *request)
{
    static const uint8_t priority[] = {
        SYS_INCHARGING_REQ,
        SYS_EXITCHARGING_REQ,
        SYS_FULL_CHARGED_REQ,
        SYS_WAKEUP_REQ,
        SYS_CUP_WAKEUP_REQ,
        SYS_SLEEP_PROBE_ERROR_REQ,
        SYS_SLEEP_PROBE_RECOVERED_REQ,
        SYS_SLEEP_PROBE_RETRY_REQ,
    };

    if(request == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    for(uint8_t index = 0U; index < (sizeof(priority) / sizeof(priority[0])); index++) {
        uint32_t mask = 1UL << priority[index];
        if((s_fsm_critical_requests & mask) != 0U) {
            s_fsm_critical_requests &= ~mask;
            *request = priority[index];
            taskEXIT_CRITICAL();
            return true;
        }
    }
    taskEXIT_CRITICAL();
    return false;
}


__WEAK void regularity_task(void * argument) {
    for(;;) {
        vTaskDelay(1);
    }
}


__WEAK void store_task(void *argument) {
    for(;;) {
        vTaskDelay(1);
    }
}


__WEAK void hci_task(void *argument) {
    for(;;) {
        vTaskDelay(1);
    }
}


__WEAK void bat_task(void * argument) {
    for(;;) {
        vTaskDelay(1);
    }
}


__WEAK void disp_task(void * argument) {
    for(;;) {
        vTaskDelay(1);
    }
}


__WEAK void mass_task(void * argument) {
    for(;;) {
        vTaskDelay(1);
    }
}


__WEAK void FSM_task(void * argument) {
	for(;;) {
        vTaskDelay(1);
    }
}


__WEAK void FSMreq_task(void * argument) {
	for(;;) {
        vTaskDelay(1);
    }
}


__WEAK void timingreq_task(void * argument) {
    for(;;) {
        vTaskDelay(1);
    }
}

__WEAK void debug_task(void *argument) {
    vTaskSuspend(NULL);
    for(;;) {
        vTaskDelay(1);
    }
}
/**
 * @}
 */
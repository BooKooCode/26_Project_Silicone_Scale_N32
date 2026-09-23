#include "app_freertos.h"
#include "dev_cs1237.h"
#include "main.h"
#include "n32l40x_pwr.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stdint.h>

#define TEST_STOP2_MIN_IDLE_TICKS 5U

volatile uint32_t g_test_stop2_entry_count;

static TickType_t limit_idle_ticks(TickType_t expected_idle_ticks)
{
    uint64_t maximum = ((uint64_t)RTC_WAKEUP_COUNTER_MAX * configTICK_RATE_HZ) /
                       RTC_WAKEUP_CLOCK_HZ;

    return ((uint64_t)expected_idle_ticks > maximum) ? (TickType_t)maximum :
                                                       expected_idle_ticks;
}

static uint32_t wakeup_counter(TickType_t expected_idle_ticks)
{
    uint64_t counts = ((uint64_t)expected_idle_ticks * RTC_WAKEUP_CLOCK_HZ +
                       configTICK_RATE_HZ - 1U) / configTICK_RATE_HZ;

    if (counts == 0U) {
        counts = 1U;
    } else if (counts > RTC_WAKEUP_COUNTER_MAX) {
        counts = RTC_WAKEUP_COUNTER_MAX;
    }
    return (uint32_t)(counts - 1U);
}

static TickType_t elapsed_ticks(uint32_t before, uint32_t after)
{
    uint32_t subticks_per_day = 24U * 60U * 60U * RTC_SUBTICKS_PER_SECOND;
    uint32_t elapsed = (after >= before) ? (after - before) :
                                         (subticks_per_day - before + after);
    uint64_t ticks = (uint64_t)elapsed * configTICK_RATE_HZ /
                     RTC_SUBTICKS_PER_SECOND;

    return (ticks > portMAX_DELAY) ? portMAX_DELAY : (TickType_t)ticks;
}

static __attribute__((noinline, section(".RamFunc"))) bool enter_stop2(void)
{
    uint32_t attempts = 1000000U;
    uint32_t previous_mode;

    while ((PWR->STS2 & PWR_STS2_MRF) == 0U) {
        if (--attempts == 0U) {
            return false;
        }
    }
    PWR->CTRL3 = (PWR->CTRL3 & ~(uint32_t)PWR_CTRL3_RAMRETMASK) |
                 PWR_CTRL3_RAM1RET | PWR_CTRL3_RAM2RET;
    previous_mode = PWR->CTRL1 & PWR_CTRL1_LPMSELMASK;
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

void vPortSuppressTicksAndSleep(TickType_t expected_idle_ticks)
{
    uint32_t before;
    uint32_t after;
    uint32_t systick_control;
    TickType_t completed;

    expected_idle_ticks = limit_idle_ticks(expected_idle_ticks);
    __disable_irq();
    __DSB();
    __ISB();

    if ((expected_idle_ticks < TEST_STOP2_MIN_IDLE_TICKS) ||
        (test_app_state() != TEST_STATE_SLEEPING) ||
        !rtc_is_ready() || !dev_cs1237_is_sleeping() ||
        dev_cs1237_transfer_active()) {
        if (eTaskConfirmSleepModeStatus() != eAbortSleep) {
            __WFI();
        }
        __enable_irq();
        return;
    }
    if (eTaskConfirmSleepModeStatus() == eAbortSleep ||
        (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U ||
        !rtc_capture_subticks(&before) ||
        !rtc_arm_wakeup(wakeup_counter(expected_idle_ticks))) {
        __enable_irq();
        return;
    }

    systick_control = SysTick->CTRL;
    SysTick->CTRL = systick_control & ~SysTick_CTRL_ENABLE_Msk;
    __DSB();
    if ((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U) {
        (void)rtc_disarm_wakeup();
        SysTick->CTRL = systick_control;
        __enable_irq();
        return;
    }

    if (enter_stop2()) {
        g_test_stop2_entry_count++;
    }
    SystemClock_RestoreFromStop2();
    PWR_ClearFlag(PWR_WKUP0_FLAG);
    PWR_ClearFlag(PWR_WKUP1_FLAG);
    PWR_ClearFlag(PWR_WKUP2_FLAG);
    if (!rtc_capture_subticks(&after)) {
        after = before;
    }
    (void)rtc_disarm_wakeup();

    SysTick->LOAD = (SystemCoreClock / configTICK_RATE_HZ) - 1U;
    SysTick->VAL = 0U;
    completed = elapsed_ticks(before, after);
    if (completed > expected_idle_ticks) {
        completed = expected_idle_ticks;
    }
    if (completed > 0U) {
        vTaskStepTick(completed - 1U);
        SCB->ICSR = SCB_ICSR_PENDSTSET_Msk;
    }
    SysTick->CTRL = systick_control;
    __enable_irq();
}

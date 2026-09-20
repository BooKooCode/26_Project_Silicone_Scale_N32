#include "rtc.h"
#include "main.h"
#include "n32l40x_exti.h"
#include "n32l40x_pwr.h"
#include "n32l40x_rcc.h"
#include "n32l40x_rtc.h"
#include "FreeRTOS.h"
#include <stddef.h>

static rtc_status_t rtc_status = RTC_STATUS_NOT_INITIALIZED;

static void rtc_wakeup_route(FunctionalState state)
{
    EXTI_InitType config;
    EXTI_InitStruct(&config);
    config.EXTI_Line = EXTI_LINE20;
    config.EXTI_Mode = EXTI_Mode_Interrupt;
    config.EXTI_Trigger = EXTI_Trigger_Rising;
    config.EXTI_LineCmd = state;
    EXTI_InitPeripheral(&config);
}

static void rtc_disable_on_failure(rtc_status_t status)
{
    NVIC_DisableIRQ(RTC_IRQn);
    rtc_wakeup_route(DISABLE);
    RTC_EnableWriteProtection(DISABLE);
    RTC->CTRL &= ~(RTC_CTRL_WTEN | RTC_CTRL_WTIEN);
    RTC_ExitInitMode();
    RTC_EnableWriteProtection(ENABLE);
    EXTI_ClrITPendBit(EXTI_LINE20);
    NVIC_ClearPendingIRQ(RTC_IRQn);
    RCC_EnableRtcClk(DISABLE);
    rtc_status = status;
}

void NS_RTC_Init(void)
{
    RTC_InitType rtc_config;
    RTC_TimeType time = {0};
    RTC_DateType date = {0};

    rtc_status = RTC_STATUS_NOT_INITIALIZED;
    NVIC_DisableIRQ(RTC_IRQn);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_AFIO, ENABLE);
    rtc_wakeup_route(DISABLE);
    PWR_BackupAccessEnable(ENABLE);
    RCC_EnableRtcClk(DISABLE);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    RCC_ConfigLse(RCC_LSE_ENABLE, 0x1FFU);
    uint32_t start_cycles = DWT->CYCCNT;
    uint32_t timeout_cycles = SystemCoreClock * 5U;
    while (RCC_GetFlagStatus(RCC_LDCTRL_FLAG_LSERD) == RESET) {
        if ((uint32_t)(DWT->CYCCNT - start_cycles) >= timeout_cycles) {
            rtc_status = RTC_STATUS_LSE_TIMEOUT;
            return;
        }
    }

    RCC_ConfigRtcClk(RCC_RTCCLK_SRC_LSE);
    if (RCC_GetRTCClkSrc() != RCC_RTCCLK_SRC_LSE) {
        rtc_status = RTC_STATUS_INIT_FAILED;
        return;
    }
    RCC_EnableRtcClk(ENABLE);
    RTC_ConfigInt(RTC_INT_WUT, DISABLE);
    if (RTC_EnableWakeUp(DISABLE) != SUCCESS) {
        rtc_disable_on_failure(RTC_STATUS_WAKEUP_FAILED);
        return;
    }
    RTC_StructInit(&rtc_config);
    rtc_config.RTC_HourFormat = RTC_24HOUR_FORMAT;
    rtc_config.RTC_AsynchPrediv = 127U;
    rtc_config.RTC_SynchPrediv = 255U;
    if (RTC_Init(&rtc_config) != SUCCESS) {
        rtc_disable_on_failure(RTC_STATUS_INIT_FAILED);
        return;
    }
    RTC_EnableBypassShadow(DISABLE);
    RTC_ConfigOutput(RTC_OUTPUT_DIS, RTC_OUTPOL_HIGH);
    date.WeekDay = 1U;
    date.Month = 1U;
    date.Date = 1U;
    if ((RTC_ConfigTime(RTC_FORMAT_BIN, &time) != SUCCESS) ||
        (RTC_SetDate(RTC_FORMAT_BIN, &date) != SUCCESS)) {
        rtc_disable_on_failure(RTC_STATUS_INIT_FAILED);
        return;
    }
    if (RTC_WaitForSynchro() != SUCCESS) {
        rtc_disable_on_failure(RTC_STATUS_SYNC_FAILED);
        return;
    }
    RTC_ConfigWakeUpClock(RTC_WKUPCLK_RTCCLK_DIV16);
    RTC_ClrFlag(RTC_FLAG_WTF);
    EXTI_ClrITPendBit(EXTI_LINE20);
    NVIC_ClearPendingIRQ(RTC_IRQn);
    rtc_status = RTC_STATUS_READY;
}

rtc_status_t rtc_get_status(void)
{
    if ((rtc_status == RTC_STATUS_READY) &&
        (RCC_GetFlagStatus(RCC_LDCTRL_FLAG_LSERD) == RESET)) {
        rtc_disable_on_failure(RTC_STATUS_LSE_LOST);
    }
    return rtc_status;
}

bool rtc_is_ready(void)
{
    return rtc_get_status() == RTC_STATUS_READY;
}

bool rtc_capture_subticks(uint32_t *subticks)
{
    if ((subticks == NULL) || !rtc_is_ready()) {
        return false;
    }
    if (RTC_WaitForSynchro() != SUCCESS) {
        rtc_disable_on_failure(RTC_STATUS_SYNC_FAILED);
        return false;
    }

    uint32_t saved_mask = __get_PRIMASK();
    __disable_irq();
    uint32_t subseconds = RTC->SUBS;
    uint32_t time = RTC->TSH;
    (void)RTC->DATE;
    __set_PRIMASK(saved_mask);

    uint32_t seconds_units = time & 0xFU;
    uint32_t minutes_units = (time >> 8U) & 0xFU;
    uint32_t hours_units = (time >> 16U) & 0xFU;
    uint32_t seconds = ((time >> 4U) & 0x7U) * 10U + seconds_units;
    uint32_t minutes = ((time >> 12U) & 0x7U) * 10U + minutes_units;
    uint32_t hours = ((time >> 20U) & 0x3U) * 10U + hours_units;
    if ((seconds_units > 9U) || (minutes_units > 9U) || (hours_units > 9U) ||
        (seconds > 59U) || (minutes > 59U) || (hours > 23U) ||
        (subseconds >= RTC_SUBTICKS_PER_SECOND) ||
        (RTC->PRE != ((127U << 16U) | (RTC_SUBTICKS_PER_SECOND - 1U)))) {
        rtc_disable_on_failure(RTC_STATUS_READ_FAILED);
        return false;
    }
    if (!rtc_is_ready()) {
        return false;
    }
    *subticks = ((hours * 60U + minutes) * 60U + seconds) * RTC_SUBTICKS_PER_SECOND
              + RTC_SUBTICKS_PER_SECOND - 1U - subseconds;
    return true;
}

bool rtc_disarm_wakeup(void)
{
    NVIC_DisableIRQ(RTC_IRQn);
    rtc_wakeup_route(DISABLE);
    if (!rtc_is_ready()) {
        return false;
    }
    RTC_ConfigInt(RTC_INT_WUT, DISABLE);
    if (RTC_EnableWakeUp(DISABLE) != SUCCESS) {
        rtc_disable_on_failure(RTC_STATUS_WAKEUP_FAILED);
        return false;
    }
    RTC_ClrFlag(RTC_FLAG_WTF);
    EXTI_ClrITPendBit(EXTI_LINE20);
    NVIC_ClearPendingIRQ(RTC_IRQn);
    return true;
}

bool rtc_arm_wakeup(uint32_t counter)
{
    if (counter >= RTC_WAKEUP_COUNTER_MAX) {
        return false;
    }
    if (!rtc_disarm_wakeup()) {
        return false;
    }
    RTC_ConfigWakeUpClock(RTC_WKUPCLK_RTCCLK_DIV16);
    RTC_SetWakeUpCounter(counter);
    NVIC_SetPriority(RTC_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    rtc_wakeup_route(ENABLE);
    RTC_ConfigInt(RTC_INT_WUT, ENABLE);
    NVIC_EnableIRQ(RTC_IRQn);
    if (RTC_EnableWakeUp(ENABLE) != SUCCESS) {
        rtc_disable_on_failure(RTC_STATUS_WAKEUP_FAILED);
        return false;
    }
    return true;
}

void RTC_WKUP_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_INT_WUT) != RESET) {
        RTC_ClrIntPendingBit(RTC_INT_WUT);
    }
    EXTI_ClrITPendBit(EXTI_LINE20);
}
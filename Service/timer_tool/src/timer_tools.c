#include "timer_tools.h"
#include "main.h"
#include "tim.h"

#ifdef MEAS_RUNTIME_LPTIM
#include "n32l40x_lptim.h"
#if !defined(MEAS_RUNTIME_LPTIM_CLOCK_HZ) || MEAS_RUNTIME_LPTIM_CLOCK_HZ < 31
#error "Configure the LPTIM counter clock frequency (at least 31 Hz)"
#endif
#if !defined(HW_DELAY_US_TIM) && !defined(HW_DELAY_US_DWT)
#error "LPTIM startup requires a microsecond delay backend"
#endif
#endif

#if defined(MEAS_RUNTIME_TIM) || defined(MEAS_RUNTIME_LPTIM)
static volatile uint32_t s_overflow_nb = 0;
#endif

#if defined(MEAS_RUNTIME_TIM) && defined(MEAS_RUNTIME_LPTIM)
#error "Select only one runtime measurement timer"
#endif

#if defined(HW_DELAY_US_TIM) && defined(HW_DELAY_US_DWT)
#error "Select only one microsecond delay backend"
#endif

void meas_tim_init(void) {
#ifdef MEAS_TIM_Init
    MEAS_TIM_Init();
#endif
#ifdef MEAS_LPTIM_Init
    MEAS_LPTIM_Init();
#endif
}

void start_meas_tim(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
#if defined(MEAS_RUNTIME_TIM) || defined(MEAS_RUNTIME_LPTIM)
    s_overflow_nb = 0U;
#endif
#ifdef MEAS_RUNTIME_TIM
    TIM_Enable(MEAS_RUNTIME_TIM, DISABLE);
    TIM_SetCnt(MEAS_RUNTIME_TIM, 0U);
    TIM_ClrIntPendingBit(MEAS_RUNTIME_TIM, TIM_INT_UPDATE);
    TIM_ConfigInt(MEAS_RUNTIME_TIM, TIM_INT_UPDATE, ENABLE);
    TIM_Enable(MEAS_RUNTIME_TIM, ENABLE);
#endif
#ifdef MEAS_RUNTIME_LPTIM
    LPTIM_Disable(MEAS_RUNTIME_LPTIM);
    LPTIM_EnableIT_ARRM(MEAS_RUNTIME_LPTIM);
    LPTIM_Enable(MEAS_RUNTIME_LPTIM);
    hw_delay_us((uint16_t)((2000000UL + MEAS_RUNTIME_LPTIM_CLOCK_HZ - 1U) /
                          MEAS_RUNTIME_LPTIM_CLOCK_HZ));
    LPTIM_ClearFLAG_ARRM(MEAS_RUNTIME_LPTIM);
    LPTIM_StartCounter(MEAS_RUNTIME_LPTIM, LPTIM_OPERATING_MODE_CONTINUOUS);
#endif
    __set_PRIMASK(primask);
}

static uint32_t meas_tim_snapshot(void) {
#ifdef MEAS_RUNTIME_TIM
    uint32_t overflows = s_overflow_nb;
    uint32_t counter = TIM_GetCnt(MEAS_RUNTIME_TIM);
    if(MEAS_RUNTIME_TIM->AR != 0xFFFFU) {
        return counter;
    }
    if(TIM_GetFlagStatus(MEAS_RUNTIME_TIM, TIM_FLAG_UPDATE) != RESET) {
        overflows++;
        counter = TIM_GetCnt(MEAS_RUNTIME_TIM);
    }
    return (overflows << 16) | counter;
#elif defined(MEAS_RUNTIME_LPTIM)
    uint32_t overflows = s_overflow_nb;
    uint32_t counter = LPTIM_GetCounter(MEAS_RUNTIME_LPTIM);
    if(LPTIM_IsActiveFlag_ARRM(MEAS_RUNTIME_LPTIM) != 0U) {
        overflows++;
        counter = LPTIM_GetCounter(MEAS_RUNTIME_LPTIM);
    }
    return (overflows << 16) | counter;
#else
    return 0U;
#endif
}

uint32_t get_meas_tim_dus(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t elapsed = meas_tim_snapshot();
    __set_PRIMASK(primask);
    return elapsed;
}

uint32_t cut_meas_tim_dus(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
#ifdef MEAS_RUNTIME_TIM
    TIM_Enable(MEAS_RUNTIME_TIM, DISABLE);
#endif
    uint32_t elapsed = meas_tim_snapshot();
#ifdef MEAS_RUNTIME_TIM
    TIM_ConfigInt(MEAS_RUNTIME_TIM, TIM_INT_UPDATE, DISABLE);
#endif
#ifdef MEAS_RUNTIME_LPTIM
    LPTIM_Disable(MEAS_RUNTIME_LPTIM);
    LPTIM_DisableIT_ARRM(MEAS_RUNTIME_LPTIM);
#endif
    __set_PRIMASK(primask);
    return elapsed;
}

void meas_tim_irq_handler(void) {
#ifdef MEAS_RUNTIME_TIM
    if(TIM_GetIntStatus(MEAS_RUNTIME_TIM, TIM_INT_UPDATE) != RESET) {
        TIM_ClrIntPendingBit(MEAS_RUNTIME_TIM, TIM_INT_UPDATE);
        s_overflow_nb++;
    }
#endif
#ifdef MEAS_RUNTIME_LPTIM
    if(LPTIM_IsEnabledIT_ARRM(MEAS_RUNTIME_LPTIM) != 0U &&
       LPTIM_IsActiveFlag_ARRM(MEAS_RUNTIME_LPTIM) != 0U) {
        LPTIM_ClearFLAG_ARRM(MEAS_RUNTIME_LPTIM);
        s_overflow_nb++;
    }
#endif
}

void hw_delay_tim_init(void) {
#ifdef HW_DELAY_US_TIM_Init
    HW_DELAY_US_TIM_Init();
#endif
#ifdef HW_DELAY_US_DWT
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
}

void hw_delay_us(uint16_t us) {
#ifdef HW_DELAY_US_TIM
    TIM_SetCnt(HW_DELAY_US_TIM, 0U);
    TIM_Enable(HW_DELAY_US_TIM, ENABLE);
    while(TIM_GetCnt(HW_DELAY_US_TIM) < us) {
        __NOP();
    }
    TIM_Enable(HW_DELAY_US_TIM, DISABLE);
#elif defined(HW_DELAY_US_DWT)
    if(us == 0U) {
        return;
    }
    hw_delay_tim_init();
    uint32_t cycles = (uint32_t)(((uint64_t)SystemCoreClock * us + 999999U) / 1000000U);
    uint32_t started = DWT->CYCCNT;
    while((uint32_t)(DWT->CYCCNT - started) < cycles) {
        __NOP();
    }
#else
    (void)us;
#endif
}

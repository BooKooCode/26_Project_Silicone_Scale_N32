#include "tim.h"
#include "FreeRTOS.h"
#include "timer_tools.h"
#include "resource_occupation.h"
#if NRFX_CHECK(DEV_BUZZER_ENABLED)
#include "dev_buzzer.h"
#endif

#if NRFX_CHECK(DEV_BUZZER_ENABLED)
void TIM6_IRQHandler(void)
{
    if(TIM_GetIntStatus(TIM6, TIM_INT_UPDATE) != RESET) {
        TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
        dev_buzzer_on_tim_elapsed();
    }
}
#endif

void NS_TIM1_Init(void)
{
    RCC_ClocksType clocks = {0};
    TIM_TimeBaseInitType timer = {0};
    OCInitType output = {0};
    GPIO_InitType gpio = {0};

    RCC_GetClocksFreqValue(&clocks);
    uint32_t timer_clock = clocks.Pclk2Freq;
    if(clocks.Pclk2Freq != clocks.HclkFreq) {
        timer_clock *= 2U;
    }
    if(timer_clock < 1000000U || timer_clock % 1000000U != 0U ||
       timer_clock / 1000000U > 65536U) {
        Error_Handler();
        return;
    }

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_TIM1 | RCC_APB2_PERIPH_GPIOA |
                           RCC_APB2_PERIPH_AFIO, ENABLE);
    TIM_DeInit(TIM1);
    timer.Prescaler = (uint16_t)(timer_clock / 1000000U - 1U);
    timer.Period = 0xFFFFU;
    timer.CntMode = TIM_CNT_MODE_UP;
    timer.ClkDiv = TIM_CLK_DIV1;
    TIM_InitTimeBase(TIM1, &timer);
    output.OcMode = TIM_OCMODE_PWM1;
    output.OutputState = TIM_OUTPUT_STATE_ENABLE;
    output.OcPolarity = TIM_OC_POLARITY_HIGH;
    TIM_InitOc1(TIM1, &output);
    TIM_ConfigOc1Preload(TIM1, TIM_OC_PRE_LOAD_DISABLE);
    TIM_ClrIntPendingBit(TIM1, TIM_INT_UPDATE);

#if (DEV_BUZZER_ACTIVE_LEVEL == 1)
    GPIO_ResetBits(BEEP_GPIO_Port, BEEP_Pin);
#else
    GPIO_SetBits(BEEP_GPIO_Port, BEEP_Pin);
#endif
    gpio.Pin = BEEP_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pull = GPIO_No_Pull;
    gpio.GPIO_Current = GPIO_DC_2mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(BEEP_GPIO_Port, &gpio);
}

void NS_TIM6_Init(void)
{
    RCC_ClocksType clocks = {0};
    TIM_TimeBaseInitType timer = {0};

    RCC_GetClocksFreqValue(&clocks);
    uint32_t timer_clock = clocks.Pclk1Freq;
    if(clocks.Pclk1Freq != clocks.HclkFreq) {
        timer_clock *= 2U;
    }
    if(timer_clock < 1000U || timer_clock % 1000U != 0U ||
       timer_clock / 1000U > 65536U) {
        Error_Handler();
        return;
    }

    NVIC_DisableIRQ(TIM6_IRQn);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM6, ENABLE);
    TIM_DeInit(TIM6);
    timer.Prescaler = (uint16_t)(timer_clock / 1000U - 1U);
    timer.Period = 99U;
    timer.CntMode = TIM_CNT_MODE_UP;
    timer.ClkDiv = TIM_CLK_DIV1;
    TIM_InitTimeBase(TIM6, &timer);
    TIM_SetCnt(TIM6, 0U);
    TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
    NVIC_ClearPendingIRQ(TIM6_IRQn);
    NVIC_SetPriority(TIM6_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
#if NRFX_CHECK(DEV_BUZZER_ENABLED)
    NVIC_EnableIRQ(TIM6_IRQn);
#endif
}

#ifdef MEAS_RUNTIME_TIM
void TIM7_IRQHandler(void)
{
    if(MEAS_RUNTIME_TIM == TIM7) {
        meas_tim_irq_handler();
    }
}
#endif

#ifdef MEAS_RUNTIME_LPTIM
void LPTIM_WKUP_IRQHandler(void)
{
    meas_tim_irq_handler();
}
#endif

void NS_TIM7_Init(void)
{
    RCC_ClocksType clocks = {0};
    TIM_TimeBaseInitType timer = {0};

    RCC_GetClocksFreqValue(&clocks);
    uint32_t timer_clock = clocks.Pclk1Freq;
    if(clocks.Pclk1Freq != clocks.HclkFreq) {
        timer_clock *= 2U;
    }
    if(timer_clock < 1000000U || timer_clock % 1000000U != 0U ||
       timer_clock / 1000000U > 65536U) {
        Error_Handler();
        return;
    }

    NVIC_DisableIRQ(TIM7_IRQn);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM7, ENABLE);
    TIM_DeInit(TIM7);
    timer.Prescaler = (uint16_t)(timer_clock / 1000000U - 1U);
    timer.Period = 0xFFFFU;
    timer.CntMode = TIM_CNT_MODE_UP;
    timer.ClkDiv = TIM_CLK_DIV1;
    TIM_InitTimeBase(TIM7, &timer);
    TIM_SetCnt(TIM7, 0U);
    TIM_ClrIntPendingBit(TIM7, TIM_INT_UPDATE);
    NVIC_ClearPendingIRQ(TIM7_IRQn);
    NVIC_SetPriority(TIM7_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(TIM7_IRQn);
}
#include "main.h"
#include "cmsis_os2.h"
#include "app_freertos.h"
#include "app_error.h"
#include "app.h"
#include "log.h"
#include "gpio.h"
#include "adc.h"
#include "spi.h"
#include "tim.h"
#include "timer_tools.h"
#include "crc.h"
#include "n32g43x_flash.h"
#include "n32g43x_pwr.h"
#include "n32g43x_rcc.h"
#include "n32g43x_dbg.h"
#include "rtc.h"

osThreadId_t initTaskHandle;
static const osThreadAttr_t initTask_attributes = {
    .name = "initTask",
    .priority = osPriorityAboveNormal,
    .stack_size = 512U * 4U
};

static void StartInitTask(void *argument)
{
    (void)argument;

    system_device_init();
    system_task_init();
    NVIC_EnableIRQ(CHARGING_ENABLE_EXTI_IRQn);
    osThreadExit();
}

static void clock_wait_register(volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    uint32_t attempts = 1000000U;

    while ((*reg & mask) != expected) {
        if (--attempts == 0U) {
            Error_Handler();
        }
    }
}

void SystemClock_Config(void)
{
    RCC_ClocksType clocks;

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);
    clock_wait_register(&PWR->STS2, PWR_STS2_MRF, PWR_STS2_MRF);
    if ((PWR->CTRL1 & PWR_CTRL1_MRSEL) == PWR_CTRL1_MRSEL2) {
        PWR->CTRL1 = (PWR->CTRL1 & ~(uint32_t)PWR_CTRL1_MRSEL) | PWR_CTRL1_MRSEL1;
        clock_wait_register(&PWR->STS2, PWR_STS2_MRF, PWR_STS2_MRF);
    }
    if ((PWR->CTRL1 & PWR_CTRL1_MRSEL) != PWR_CTRL1_MRSEL1) {
        Error_Handler();
    }
    RCC_EnableHsi(ENABLE);
    clock_wait_register(&RCC->CTRL, RCC_CTRL_HSIRDF, RCC_CTRL_HSIRDF);
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_HSI);
    while (RCC_GetSysclkSrc() != 0x04U) {
    }
    SystemCoreClockUpdate();

    RCC_ConfigHse(RCC_HSE_ENABLE);
    clock_wait_register(&RCC->CTRL, RCC_CTRL_HSERDF, RCC_CTRL_HSERDF);
    FLASH_SetLatency(FLASH_LATENCY_2);
    clock_wait_register(&FLASH->AC, FLASH_AC_LATENCY, FLASH_LATENCY_2);
    RCC_ConfigPclk1(RCC_HCLK_DIV4);
    RCC_ConfigPclk2(RCC_HCLK_DIV2);
    RCC_ConfigHclk(RCC_SYSCLK_DIV1);
    RCC_EnablePll(DISABLE);
    RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV1, RCC_PLL_MUL_8, RCC_PLLDIVCLK_DISABLE);
    RCC_EnablePll(ENABLE);
    clock_wait_register(&RCC->CTRL, RCC_CTRL_PLLRDF, RCC_CTRL_PLLRDF);
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);
    while (RCC_GetSysclkSrc() != 0x0CU) {
    }
    __DSB();
    __ISB();
    SystemCoreClockUpdate();
    RCC_GetClocksFreqValue(&clocks);
}

void SystemClock_RestoreFromStop2(void)
{
    SystemClock_Config();
}

static void power_latch_early(void)
{
    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_IOPAEN;
    (void)RCC->APB2PCLKEN;
    GPIOA->PBSC = HW_POWERON_Pin;
    GPIOA->POTYPE &= ~(uint32_t)GPIO_POTYPE_POT_4;
    GPIOA->PUPD &= ~(uint32_t)GPIO_PUPD4_Msk;
    GPIOA->PMODE = (GPIOA->PMODE & ~(uint32_t)GPIO_PMODE4_Msk) | GPIO_PMODE4_1;
    __DSB();
}

#ifndef DEBUG_KEEP_LOWPOWER_SWD
#if defined(DEBUG)
#define DEBUG_KEEP_LOWPOWER_SWD 1
#else
#define DEBUG_KEEP_LOWPOWER_SWD 0
#endif
#endif

int main(void)
{
    power_latch_early();
#if DEBUG_KEEP_LOWPOWER_SWD
    DBG_ConfigPeriph(DBG_SLEEP | DBG_STOP | DBG_STDBY, ENABLE);
#else
    DBG_ConfigPeriph(DBG_SLEEP | DBG_STOP | DBG_STDBY, DISABLE);
#endif
    SystemClock_Config();
    NS_GPIO_Init();
    NS_RTC_Init();
    NS_ADC1_Init();
    spi_flash_init();
    NS_TIM1_Init();
    NS_CRC_Init();
    hw_delay_tim_init();
    NS_TIM7_Init();
    NS_TIM6_Init();
    NS_SPI1_Init();

    if(osKernelInitialize() != osOK) {
        Error_Handler();
    }
    NS_FREERTOS_Init();

    if(log_init() != ERR_NONE) {
        Error_Handler();
    }

    initTaskHandle = osThreadNew(StartInitTask, NULL, &initTask_attributes);
    if(initTaskHandle == NULL) {
        Error_Handler();
    }

    osKernelStart();
    Error_Handler();
}

void Error_Handler(void)
{
    uint32_t caller_pc = 0U;
#if defined(__GNUC__) || defined(__clang__)
    caller_pc = (uint32_t)(uintptr_t)__builtin_return_address(0);
#endif
    __disable_irq();
    app_error_trace_record_from_error_handler(caller_pc);
    while(1) {
        __WFI();
    }
}

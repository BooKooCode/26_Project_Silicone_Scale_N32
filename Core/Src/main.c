#include "main.h"
#include "cmsis_os2.h"
#include "app_freertos.h"
#include "app_error.h"
#include "app.h"
#include "log.h"
#include "gpio.h"
#include "rtc.h"
#include "adc.h"
#include "spi.h"
#include "tim.h"
#include "timer_tools.h"
#include "crc.h"
#include "n32l40x_flash.h"
#include "n32l40x_rcc.h"

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

int main(void)
{
    RCC->APB2PCLKEN |= RCC_APB2PCLKEN_IOPAEN;
    (void)RCC->APB2PCLKEN;
    __DSB();
    GPIOA->PBSC = 1UL << 4;
    GPIOA->POTYPE &= ~(uint32_t)GPIO_POTYPE_POT_4;
    GPIOA->PUPD &= ~(uint32_t)GPIO_PUPD4_Msk;
    GPIOA->PMODE = (GPIOA->PMODE & ~(uint32_t)GPIO_PMODE4_Msk) | GPIO_PMODE4_1;
    __DSB();

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

    if (osKernelInitialize() != osOK) {
        Error_Handler();
    }
    NS_FREERTOS_Init();

    if (log_init() != ERR_NONE) {
        Error_Handler();
    }

    initTaskHandle = osThreadNew(StartInitTask, NULL, &initTask_attributes);
    if (initTaskHandle == NULL) {
        Error_Handler();
    }

    osKernelStart();
    Error_Handler();
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

static void system_clock_config(bool stop2_recovery)
{
    RCC_ClocksType clocks;

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_PWR, ENABLE);
    clock_wait_register(&PWR->STS2, PWR_STS2_MRF, PWR_STS2_MRF);
    if ((PWR->CTRL1 & PWR_CTRL1_MRSEL) != PWR_CTRL1_MRSEL2) {
        Error_Handler();
    }

    RCC_EnableHsi(ENABLE);
    clock_wait_register(&RCC->CTRL, RCC_CTRL_HSIRDF, RCC_CTRL_HSIRDF);
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_HSI);
    clock_wait_register(&RCC->CFG, RCC_CFG_SCLKSTS, RCC_SYSCLK_SRC_HSI << 2U);
    SystemCoreClockUpdate();

    RCC_EnablePll(DISABLE);
    clock_wait_register(&RCC->CTRL, RCC_CTRL_PLLRDF, 0U);
    RCC_ConfigHse(RCC_HSE_ENABLE);
    clock_wait_register(&RCC->CTRL, RCC_CTRL_HSERDF, RCC_CTRL_HSERDF);
    FLASH_SetLatency(FLASH_LATENCY_1);
    clock_wait_register(&FLASH->AC, FLASH_AC_LATENCY, FLASH_LATENCY_1);
    RCC_ConfigPclk1(RCC_HCLK_DIV4);
    RCC_ConfigPclk2(RCC_HCLK_DIV2);
    RCC_ConfigHclk(RCC_SYSCLK_DIV1);
    RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV1,
                 stop2_recovery ? RCC_PLL_MUL_16 : RCC_PLL_MUL_8,
                 stop2_recovery ? RCC_PLLDIVCLK_ENABLE : RCC_PLLDIVCLK_DISABLE);
    RCC_EnablePll(ENABLE);
    clock_wait_register(&RCC->CTRL, RCC_CTRL_PLLRDF, RCC_CTRL_PLLRDF);
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);
    clock_wait_register(&RCC->CFG, RCC_CFG_SCLKSTS, RCC_SYSCLK_SRC_PLLCLK << 2U);
    __DSB();
    __ISB();
    SystemCoreClockUpdate();
    RCC_GetClocksFreqValue(&clocks);
    if ((SystemCoreClock != 64000000U) || (clocks.SysclkFreq != 64000000U) ||
        (clocks.HclkFreq != 64000000U) || (clocks.Pclk1Freq != 16000000U) ||
        (clocks.Pclk2Freq != 32000000U)) {
        Error_Handler();
    }
}

void SystemClock_Config(void)
{
    system_clock_config(false);
}

void SystemClock_RestoreFromStop2(void)
{
    system_clock_config(true);
}

void Error_Handler(void)
{
    __disable_irq();
    app_error_trace_record_from_error_handler();
    while (1) {
        __WFI();
    }
}
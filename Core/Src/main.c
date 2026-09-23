#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_freertos.h"
#include "auto_version.h"
#include "crc.h"
#include "n32l40x_flash.h"
#include "n32l40x_pwr.h"
#include "n32l40x_rcc.h"
#include "rtc.h"

extern void xPortSysTickHandler(void);

static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

static void retain_version_metadata(void)
{
    volatile uint8_t marker = (uint8_t)(TAG_VERSION[0] ^ COMMIT_TIME[0] ^
                                        LAST_COMPILE_TIME[0] ^ LAST_COMPILE_EMAIL[0]);
    (void)marker;
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
    RCC_ConfigPclk1(RCC_HCLK_DIV4);
    RCC_ConfigPclk2(RCC_HCLK_DIV2);
    RCC_ConfigHclk(RCC_SYSCLK_DIV1);
    RCC_ConfigPll(RCC_PLL_SRC_HSE_DIV1, RCC_PLL_MUL_8, RCC_PLLDIVCLK_DISABLE);
    RCC_EnablePll(ENABLE);
    clock_wait_register(&RCC->CTRL, RCC_CTRL_PLLRDF, RCC_CTRL_PLLRDF);
    RCC_ConfigSysclk(RCC_SYSCLK_SRC_PLLCLK);
    clock_wait_register(&RCC->CFG, RCC_CFG_SCLKSTS, RCC_SYSCLK_SRC_PLLCLK << 2U);
    SystemCoreClockUpdate();
    RCC_GetClocksFreqValue(&clocks);
    if ((SystemCoreClock != 64000000U) || (clocks.Pclk1Freq != 16000000U) ||
        (clocks.Pclk2Freq != 32000000U)) {
        Error_Handler();
    }
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

int main(void)
{
    retain_version_metadata();
    power_latch_early();
    SystemClock_Config();
#if TEST_SHUTDOWN_MODE == TEST_SHUTDOWN_CUP_WAKE
    NS_RTC_Init();
#endif
    NS_CRC_Init();
    NS_FREERTOS_Init();
    vTaskStartScheduler();
    Error_Handler();
}

void SysTick_Handler(void)
{
    xPortSysTickHandler();
}

void vApplicationGetIdleTaskMemory(StaticTask_t **task_buffer,
                                   StackType_t **stack_buffer,
                                   uint32_t *stack_size)
{
    *task_buffer = &idle_task_tcb;
    *stack_buffer = idle_task_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    Error_Handler();
}

void app_rtos_assert_failed(const char *file, int line)
{
    (void)file;
    (void)line;
    Error_Handler();
}

void Error_Handler(void)
{
    __disable_irq();
    for (;;) {
        __WFI();
    }
}

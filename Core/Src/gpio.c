#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bat_meas.h"

static volatile bool s_sleep_left_wakeup_enabled = false;
static volatile bool s_sleep_left_wakeup_pending = false;

void NS_GPIO_Init(void)
{
    GPIO_InitType gpio = {0};

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOB |
                           RCC_APB2_PERIPH_AFIO, ENABLE);

    GPIO_SetBits(HW_POWERON_GPIO_Port, HW_POWERON_Pin);
    GPIO_SetBits(W25QXX_NS_GPIO_Port, W25QXX_NS_Pin);
    GPIO_ResetBits(GPIOA, ADC_Control_Pin | SEN_SCK_Pin | BEEP_Pin);
    GPIO_ResetBits(GPIOB, DIS_SCK_Pin | DIS_MOSI_Pin);

    gpio.Pin = ADC_Control_Pin | SEN_SCK_Pin | BEEP_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pull = GPIO_No_Pull;
    gpio.GPIO_Current = GPIO_DC_2mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(GPIOA, &gpio);

    gpio.Pin = W25QXX_NS_Pin | DIS_SCK_Pin | DIS_MOSI_Pin;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    GPIO_InitPeripheral(GPIOB, &gpio);

    gpio.Pin = btn_left_Pin | btn_right_Pin | SEN_MISO_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Input;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(GPIOA, &gpio);

    gpio.Pin = BAT_MEAS_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Analog;
    GPIO_InitPeripheral(BAT_MEAS_GPIO_Port, &gpio);

    gpio.Pin = FULL_CHARGED_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Input;
    gpio.GPIO_Pull = GPIO_Pull_Up;
    GPIO_InitPeripheral(FULL_CHARGED_GPIO_Port, &gpio);

    NVIC_DisableIRQ(CHARGING_ENABLE_EXTI_IRQn);
    GPIO_ConfigEXTILine(GPIOB_PORT_SOURCE, GPIO_PIN_SOURCE2);
    gpio.Pin = CHARGING_ENABLE_Pin;
    gpio.GPIO_Mode = GPIO_Mode_IT_Rising_Falling;
    GPIO_InitPeripheral(CHARGING_ENABLE_GPIO_Port, &gpio);
    EXTI_ClrITPendBit(EXTI_LINE2);
    NVIC_ClearPendingIRQ(CHARGING_ENABLE_EXTI_IRQn);
    NVIC_SetPriority(CHARGING_ENABLE_EXTI_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
}

void gpio_sleep_left_wakeup_enable(void)
{
    GPIO_InitType gpio = {0};

    NVIC_DisableIRQ(CHARGING_ENABLE_EXTI_IRQn);
    GPIO_ConfigEXTILine(GPIOA_PORT_SOURCE, GPIO_PIN_SOURCE2);
    gpio.Pin = btn_left_Pin;
    gpio.GPIO_Mode = GPIO_Mode_IT_Falling;
    gpio.GPIO_Pull = GPIO_No_Pull;
    gpio.GPIO_Current = GPIO_DC_2mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(btn_left_GPIO_Port, &gpio);
    EXTI_ClrITPendBit(EXTI_LINE2);
    NVIC_ClearPendingIRQ(CHARGING_ENABLE_EXTI_IRQn);
    s_sleep_left_wakeup_pending = false;
    s_sleep_left_wakeup_enabled = true;
    NVIC_SetPriority(CHARGING_ENABLE_EXTI_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(CHARGING_ENABLE_EXTI_IRQn);
}

void gpio_sleep_left_wakeup_disable(void)
{
    GPIO_InitType gpio = {0};

    s_sleep_left_wakeup_enabled = false;
    s_sleep_left_wakeup_pending = false;
    NVIC_DisableIRQ(CHARGING_ENABLE_EXTI_IRQn);
    GPIO_ConfigEXTILine(GPIOB_PORT_SOURCE, GPIO_PIN_SOURCE2);
    gpio.Pin = CHARGING_ENABLE_Pin;
    gpio.GPIO_Mode = GPIO_Mode_IT_Rising_Falling;
    gpio.GPIO_Pull = GPIO_No_Pull;
    gpio.GPIO_Current = GPIO_DC_2mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(CHARGING_ENABLE_GPIO_Port, &gpio);
    EXTI_ClrITPendBit(EXTI_LINE2);
    NVIC_ClearPendingIRQ(CHARGING_ENABLE_EXTI_IRQn);
    NVIC_SetPriority(CHARGING_ENABLE_EXTI_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(CHARGING_ENABLE_EXTI_IRQn);
}

bool gpio_sleep_left_wakeup_pending(void)
{
    bool pending;
    taskENTER_CRITICAL();
    pending = s_sleep_left_wakeup_pending;
    s_sleep_left_wakeup_pending = false;
    taskEXIT_CRITICAL();
    return pending;
}

void EXTI2_IRQHandler(void)
{
    if (EXTI_GetITStatus(EXTI_LINE2) != RESET) {
        EXTI_ClrITPendBit(EXTI_LINE2);
        if (s_sleep_left_wakeup_enabled) {
            if (GPIO_ReadInputDataBit(btn_left_GPIO_Port, btn_left_Pin) == Bit_RESET) {
                s_sleep_left_wakeup_pending = true;
                EXTI->IMASK &= ~(uint32_t)EXTI_LINE2;
            }
        } else {
            bat_gpio_exti_handler(CHARGING_ENABLE_Pin);
        }
    }
}

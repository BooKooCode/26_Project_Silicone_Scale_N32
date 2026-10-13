#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bat_meas.h"
#include "os_mgnt.h"

static volatile bool s_sleep_left_wakeup_enabled = false;
static volatile bool s_sleep_left_wakeup_configured = false;
static volatile bool s_sleep_left_wakeup_pending = false;

#define GPIO_SLEEP_KEEP_A (btn_left_Pin | HW_POWERON_Pin | SEN_SCK_Pin | \
                           SEN_MISO_Pin | GPIO_PIN_13 | GPIO_PIN_14)
#define GPIO_SLEEP_KEEP_B (CHARGING_ENABLE_Pin)
#define GPIO_SLEEP_ANALOG_A ((uint16_t)(GPIO_PIN_ALL & ~GPIO_SLEEP_KEEP_A))
#define GPIO_SLEEP_ANALOG_B ((uint16_t)(GPIO_PIN_ALL & ~GPIO_SLEEP_KEEP_B))

typedef struct {
    uint32_t pmode;
    uint32_t potype;
    uint32_t sr;
    uint32_t pupd;
    uint32_t afl;
    uint32_t afh;
    uint32_t ds;
} gpio_sleep_snapshot_t;

static gpio_sleep_snapshot_t s_gpioa_sleep_snapshot;
static gpio_sleep_snapshot_t s_gpiob_sleep_snapshot;
static bool s_sleep_unused_pins_configured = false;

static uint32_t gpio_expand_pin_mask(uint16_t pins, uint8_t width, uint8_t first_pin,
                                     uint8_t pin_count)
{
    uint32_t mask = 0U;

    for(uint8_t index = 0U; index < pin_count; index++) {
        if((pins & ((uint16_t)1U << (first_pin + index))) != 0U) {
            mask |= ((1UL << width) - 1UL) << (index * width);
        }
    }
    return mask;
}

static void gpio_sleep_snapshot(GPIO_Module *port, gpio_sleep_snapshot_t *snapshot)
{
    snapshot->pmode = port->PMODE;
    snapshot->potype = port->POTYPE;
    snapshot->sr = port->SR;
    snapshot->pupd = port->PUPD;
    snapshot->afl = port->AFL;
    snapshot->afh = port->AFH;
    snapshot->ds = port->DS;
}

static void gpio_sleep_restore(GPIO_Module *port, const gpio_sleep_snapshot_t *snapshot,
                               uint16_t pins)
{
    uint32_t mask2 = gpio_expand_pin_mask(pins, 2U, 0U, 16U);
    uint32_t mask4_low = gpio_expand_pin_mask(pins, 4U, 0U, 8U);
    uint32_t mask4_high = gpio_expand_pin_mask(pins, 4U, 8U, 8U);

    port->PMODE = (port->PMODE & ~mask2) | (snapshot->pmode & mask2);
    port->POTYPE = (port->POTYPE & ~(uint32_t)pins) |
                   (snapshot->potype & (uint32_t)pins);
    port->SR = (port->SR & ~mask2) | (snapshot->sr & mask2);
    port->PUPD = (port->PUPD & ~mask2) | (snapshot->pupd & mask2);
    port->AFL = (port->AFL & ~mask4_low) | (snapshot->afl & mask4_low);
    port->AFH = (port->AFH & ~mask4_high) | (snapshot->afh & mask4_high);
    port->DS = (port->DS & ~mask2) | (snapshot->ds & mask2);
}

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

    gpio.Pin = btn_left_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Input;
    gpio.GPIO_Pull = GPIO_Pull_Up;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(GPIOA, &gpio);

    gpio.Pin = btn_right_Pin;
    gpio.GPIO_Pull = GPIO_Pull_Down;
    GPIO_InitPeripheral(GPIOA, &gpio);

    gpio.Pin = SEN_MISO_Pin;
    gpio.GPIO_Pull = GPIO_No_Pull;
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
#if !SLEEP_LEFT_KEY_WAKE_ENABLED
    s_sleep_left_wakeup_configured = true;
#else
    GPIO_InitType gpio = {0};

    NVIC_DisableIRQ(CHARGING_ENABLE_EXTI_IRQn);
    GPIO_ConfigEXTILine(GPIOA_PORT_SOURCE, GPIO_PIN_SOURCE2);
    gpio.Pin = btn_left_Pin;
    gpio.GPIO_Mode = GPIO_Mode_IT_Falling;
    gpio.GPIO_Pull = GPIO_Pull_Up;
    gpio.GPIO_Current = GPIO_DC_2mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(btn_left_GPIO_Port, &gpio);
    EXTI_ClrITPendBit(EXTI_LINE2);
    NVIC_ClearPendingIRQ(CHARGING_ENABLE_EXTI_IRQn);
    s_sleep_left_wakeup_pending = false;
    s_sleep_left_wakeup_configured = true;
    s_sleep_left_wakeup_enabled =
        (GPIO_ReadInputDataBit(btn_left_GPIO_Port, btn_left_Pin) != Bit_RESET);
    NVIC_SetPriority(CHARGING_ENABLE_EXTI_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(CHARGING_ENABLE_EXTI_IRQn);
#endif
}

void gpio_sleep_left_wakeup_disable(void)
{
    GPIO_InitType gpio = {0};

    s_sleep_left_wakeup_enabled = false;
    s_sleep_left_wakeup_configured = false;
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

void gpio_sleep_unused_pins_enter(void)
{
    GPIO_InitType gpio = {0};

    taskENTER_CRITICAL();
    if(!s_sleep_unused_pins_configured) {
        gpio_sleep_snapshot(GPIOA, &s_gpioa_sleep_snapshot);
        gpio_sleep_snapshot(GPIOB, &s_gpiob_sleep_snapshot);
        gpio.GPIO_Mode = GPIO_Mode_Analog;
        gpio.GPIO_Pull = GPIO_No_Pull;
        gpio.GPIO_Current = GPIO_DC_2mA;
        gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
        gpio.Pin = GPIO_SLEEP_ANALOG_A;
        GPIO_InitPeripheral(GPIOA, &gpio);
        gpio.Pin = GPIO_SLEEP_ANALOG_B;
        GPIO_InitPeripheral(GPIOB, &gpio);
        s_sleep_unused_pins_configured = true;
    }
    taskEXIT_CRITICAL();
}

void gpio_sleep_unused_pins_restore(void)
{
    taskENTER_CRITICAL();
    if(s_sleep_unused_pins_configured) {
        gpio_sleep_restore(GPIOA, &s_gpioa_sleep_snapshot, GPIO_SLEEP_ANALOG_A);
        gpio_sleep_restore(GPIOB, &s_gpiob_sleep_snapshot, GPIO_SLEEP_ANALOG_B);
        s_sleep_unused_pins_configured = false;
    }
    taskEXIT_CRITICAL();
}

void gpio_sleep_left_wakeup_arm_if_released(void)
{
#if SLEEP_LEFT_KEY_WAKE_ENABLED
    if(!s_sleep_left_wakeup_configured || s_sleep_left_wakeup_enabled ||
       (GPIO_ReadInputDataBit(btn_left_GPIO_Port, btn_left_Pin) == Bit_RESET)) {
        return;
    }
    EXTI_ClrITPendBit(EXTI_LINE2);
    NVIC_ClearPendingIRQ(CHARGING_ENABLE_EXTI_IRQn);
    EXTI->IMASK |= EXTI_LINE2;
    s_sleep_left_wakeup_enabled = true;
#endif
}

bool gpio_sleep_left_wakeup_is_enabled(void)
{
    return s_sleep_left_wakeup_enabled;
}

bool gpio_sleep_left_wakeup_wait_ready(void)
{
#if SLEEP_LEFT_KEY_WAKE_ENABLED
    return s_sleep_left_wakeup_enabled;
#else
    return s_sleep_left_wakeup_configured;
#endif
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

bool gpio_sleep_left_wakeup_capture_level(void)
{
    if(!s_sleep_left_wakeup_enabled ||
       (GPIO_ReadInputDataBit(btn_left_GPIO_Port, btn_left_Pin) != Bit_RESET)) {
        return false;
    }
    s_sleep_left_wakeup_pending = true;
    s_sleep_left_wakeup_enabled = false;
    EXTI->IMASK &= ~(uint32_t)EXTI_LINE2;
    EXTI_ClrITPendBit(EXTI_LINE2);
    NVIC_ClearPendingIRQ(CHARGING_ENABLE_EXTI_IRQn);
    return true;
}

void EXTI2_IRQHandler(void)
{
    if((EXTI->PEND & EXTI_LINE2) != 0U) {
        EXTI_ClrITPendBit(EXTI_LINE2);
        if(SLEEP_LEFT_KEY_WAKE_ENABLED && s_sleep_left_wakeup_configured) {
            if(s_sleep_left_wakeup_enabled &&
               (GPIO_ReadInputDataBit(btn_left_GPIO_Port, btn_left_Pin) == Bit_RESET)) {
                s_sleep_left_wakeup_pending = true;
                s_sleep_left_wakeup_enabled = false;
                EXTI->IMASK &= ~(uint32_t)EXTI_LINE2;
                os_mgnt_notify_task_from_isr(OS_TASK_HCI);
            }
        } else {
            bat_gpio_exti_handler(CHARGING_ENABLE_Pin);
        }
    }
}

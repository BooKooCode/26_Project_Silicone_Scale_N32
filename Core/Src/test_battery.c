#include "test_battery.h"
#include "main.h"
#include "n32l40x_adc.h"
#include "n32l40x_rcc.h"
#include "timer_tools.h"
#include <stdbool.h>
#include <stdint.h>

#define ADC_TIMEOUT_US       10000U
#define BATTERY_SAMPLE_COUNT 5U

static bool initialized;

static bool timed_out(uint32_t started)
{
    uint32_t timeout = (uint32_t)(((uint64_t)SystemCoreClock * ADC_TIMEOUT_US + 999999U) /
                                  1000000U);
    return (uint32_t)(DWT->CYCCNT - started) >= timeout;
}

static bool adc_power_on(void)
{
    uint32_t started;

    ADC_Enable(ADC, ENABLE);
    hw_delay_us(8U);
    started = DWT->CYCCNT;
    while (ADC_GetFlagStatusNew(ADC, ADC_FLAG_RDY) == RESET) {
        if (timed_out(started)) {
            ADC_Enable(ADC, DISABLE);
            return false;
        }
    }
    return true;
}

void test_battery_init(void)
{
    ADC_InitType adc = {0};
    GPIO_InitType gpio = {0};
    uint32_t started;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    RCC_EnableHsi(ENABLE);
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
    gpio.Pin = BAT_MEAS_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Analog;
    GPIO_InitPeripheral(BAT_MEAS_GPIO_Port, &gpio);

    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_ADC, ENABLE);
    ADC_DeInit(ADC);
    ADC_ConfigClk(ADC_CTRL3_CKMOD_AHB, RCC_ADCHCLK_DIV16);
    RCC_ConfigAdc1mClk(RCC_ADC1MCLK_SRC_HSI, RCC_ADC1MCLK_DIV16);
    Reference_Voltage_Switch(ADC_REFENCE_Volt_VREF);
    ADC_InitStruct(&adc);
    adc.MultiChEn = DISABLE;
    adc.ContinueConvEn = DISABLE;
    adc.ExtTrigSelect = ADC_EXT_TRIGCONV_NONE;
    adc.DatAlign = ADC_DAT_ALIGN_R;
    adc.ChsNumber = 1U;
    ADC_Init(ADC, &adc);
    ADC_SetConvResultBitNum(ADC, ADC_RST_BIT_12);
    ADC_ConfigRegularChannel(ADC, ADC_CH_1_PA0, 1U, ADC_SAMP_TIME_239CYCLES5);
    ADC_EnableDMA(ADC, DISABLE);

    if (!adc_power_on()) {
        return;
    }
    ADC_StartCalibration(ADC);
    started = DWT->CYCCNT;
    while (ADC_GetCalibrationStatus(ADC) != RESET) {
        if (timed_out(started)) {
            ADC_Enable(ADC, DISABLE);
            return;
        }
    }
    ADC_Enable(ADC, DISABLE);
    initialized = true;
}

static bool sample_voltage(float *voltage)
{
    uint32_t started;

    if (!adc_power_on()) {
        return false;
    }
    ADC_ClearFlag(ADC, ADC_FLAG_ENDCA | ADC_FLAG_ENDC | ADC_FLAG_STR);
    ADC_EnableSoftwareStartConv(ADC, ENABLE);
    started = DWT->CYCCNT;
    while (ADC_GetFlagStatus(ADC, ADC_FLAG_ENDCA) == RESET) {
        if (timed_out(started)) {
            ADC_EnableSoftwareStartConv(ADC, DISABLE);
            ADC_Enable(ADC, DISABLE);
            return false;
        }
    }
    ADC_EnableSoftwareStartConv(ADC, DISABLE);
    *voltage = ((float)ADC_GetDat(ADC) * 3.3f * 2.0f) / 4095.0f;
    ADC_ClearFlag(ADC, ADC_FLAG_ENDCA | ADC_FLAG_ENDC | ADC_FLAG_STR);
    ADC_Enable(ADC, DISABLE);
    return true;
}

uint8_t test_battery_percent(void)
{
    static const float voltage_table[] = {
        4.17f, 4.06f, 3.95f, 3.86f, 3.79f, 3.71f, 3.65f, 3.62f, 3.60f, 3.55f, 3.46f
    };
    static const uint8_t percent_table[] = {
        100U, 90U, 80U, 70U, 60U, 50U, 40U, 30U, 20U, 10U, 0U
    };
    float sum = 0.0f;
    uint32_t count = 0U;

    if (!initialized) {
        return 0U;
    }
    GPIO_SetBits(ADC_Control_GPIO_Port, ADC_Control_Pin);
    hw_delay_us(1000U);
    for (uint32_t index = 0U; index < BATTERY_SAMPLE_COUNT; index++) {
        float voltage;
        if (sample_voltage(&voltage)) {
            sum += voltage;
            count++;
        }
        hw_delay_us(10000U);
    }
    GPIO_ResetBits(ADC_Control_GPIO_Port, ADC_Control_Pin);
    if (count == 0U) {
        return 0U;
    }

    float voltage = sum / (float)count;
    if (voltage > voltage_table[0]) {
        return percent_table[0];
    }
    for (uint32_t index = 1U; index < sizeof(voltage_table) / sizeof(voltage_table[0]); index++) {
        if (voltage > voltage_table[index]) {
            return percent_table[index - 1U];
        }
    }
    return 0U;
}

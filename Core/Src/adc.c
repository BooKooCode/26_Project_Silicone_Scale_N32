#include "adc.h"
#include "main.h"
#include "n32l40x_adc.h"

static bool adc_initialized = false;

void NS_ADC1_Init(void)
{
    ADC_InitType adc_config;
    GPIO_InitType gpio_config;

    adc_initialized = false;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    RCC_EnableHsi(ENABLE);
    uint32_t start_cycles = DWT->CYCCNT;
    uint32_t timeout_cycles = SystemCoreClock / 100U;
    while(RCC_GetFlagStatus(RCC_CTRL_FLAG_HSIRDF) == RESET) {
        if((uint32_t)(DWT->CYCCNT - start_cycles) >= timeout_cycles) {
            Error_Handler();
            return;
        }
    }

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
    GPIO_InitStruct(&gpio_config);
    gpio_config.Pin = BAT_MEAS_Pin;
    gpio_config.GPIO_Mode = GPIO_Mode_Analog;
    GPIO_InitPeripheral(BAT_MEAS_GPIO_Port, &gpio_config);

    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_ADC, ENABLE);
    ADC_DeInit(ADC);
    ADC_ConfigClk(ADC_CTRL3_CKMOD_AHB, RCC_ADCHCLK_DIV16);
    RCC_ConfigAdc1mClk(RCC_ADC1MCLK_SRC_HSI, RCC_ADC1MCLK_DIV16);
    Reference_Voltage_Switch(ADC_REFENCE_Volt_VREF);
    ADC_InitStruct(&adc_config);
    adc_config.MultiChEn = DISABLE;
    adc_config.ContinueConvEn = DISABLE;
    adc_config.ExtTrigSelect = ADC_EXT_TRIGCONV_NONE;
    adc_config.DatAlign = ADC_DAT_ALIGN_R;
    adc_config.ChsNumber = 1;
    ADC_Init(ADC, &adc_config);
    ADC_SetConvResultBitNum(ADC, ADC_RST_BIT_12);
    ADC_ConfigRegularChannel(ADC, ADC_CH_1_PA0, 1, ADC_SAMP_TIME_239CYCLES5);
    ADC_EnableDMA(ADC, DISABLE);
    ADC_Enable(ADC, DISABLE);
    adc_initialized = true;
}

bool adc_is_initialized(void)
{
    return adc_initialized;
}
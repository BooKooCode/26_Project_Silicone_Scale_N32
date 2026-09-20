#include "gpio.h"

void NS_GPIO_Init(void)
{
    GPIO_InitType gpio = {0};

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_GPIOB |
                           RCC_APB2_PERIPH_AFIO, ENABLE);
    GPIO_SetBits(HW_POWERON_GPIO_Port, HW_POWERON_Pin);
    GPIO_SetBits(W25QXX_NS_GPIO_Port, W25QXX_NS_Pin);
    GPIO_ResetBits(GPIOA, ADC_Control_Pin | SEN_SCK_Pin | BEEP_Pin);
    GPIO_ResetBits(GPIOB, DIS_SCK_Pin | DIS_MOSI_Pin);

    gpio.Pin = ADC_Control_Pin | HW_POWERON_Pin | SEN_SCK_Pin | BEEP_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pull = GPIO_No_Pull;
    gpio.GPIO_Current = GPIO_DC_2mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(GPIOA, &gpio);

    gpio.Pin = W25QXX_NS_Pin | DIS_SCK_Pin | DIS_MOSI_Pin;
    gpio.GPIO_Current = GPIO_DC_4mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    GPIO_InitPeripheral(GPIOB, &gpio);

    gpio.Pin = btn_left_Pin | btn_right_Pin | SEN_MISO_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Input;
    gpio.GPIO_Pull = GPIO_No_Pull;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    GPIO_InitPeripheral(GPIOA, &gpio);
}

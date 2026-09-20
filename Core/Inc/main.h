#ifndef MAIN_H
#define MAIN_H

#include "n32l40x.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BAT_MEAS_Pin GPIO_PIN_0
#define BAT_MEAS_GPIO_Port GPIOA
#define ADC_Control_Pin GPIO_PIN_7
#define ADC_Control_GPIO_Port GPIOA
#define HW_POWERON_Pin GPIO_PIN_4
#define HW_POWERON_GPIO_Port GPIOA
#define W25QXX_SCK_Pin GPIO_PIN_13
#define W25QXX_SCK_GPIO_Port GPIOB
#define W25QXX_MISO_Pin GPIO_PIN_14
#define W25QXX_MISO_GPIO_Port GPIOB
#define W25QXX_MOSI_Pin GPIO_PIN_15
#define W25QXX_MOSI_GPIO_Port GPIOB
#define W25QXX_NS_Pin GPIO_PIN_12
#define W25QXX_NS_GPIO_Port GPIOB
#define DIS_SCK_Pin GPIO_PIN_3
#define DIS_SCK_GPIO_Port GPIOB
#define DIS_MOSI_Pin GPIO_PIN_5
#define DIS_MOSI_GPIO_Port GPIOB
#define btn_left_Pin GPIO_PIN_2
#define btn_left_GPIO_Port GPIOA
#define btn_right_Pin GPIO_PIN_3
#define btn_right_GPIO_Port GPIOA
#define FULL_CHARGED_Pin GPIO_PIN_1
#define FULL_CHARGED_GPIO_Port GPIOB
#define CHARGING_ENABLE_Pin GPIO_PIN_2
#define CHARGING_ENABLE_GPIO_Port GPIOB
#define CHARGING_ENABLE_EXTI_IRQn EXTI2_IRQn
#define SEN_SCK_Pin GPIO_PIN_5
#define SEN_SCK_GPIO_Port GPIOA
#define SEN_MISO_Pin GPIO_PIN_6
#define SEN_MISO_GPIO_Port GPIOA
#define BEEP_Pin GPIO_PIN_8
#define BEEP_GPIO_Port GPIOA

void SystemClock_Config(void);
void SystemClock_RestoreFromStop2(void);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif
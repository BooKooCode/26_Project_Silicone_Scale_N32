/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include <assert.h>
#include <stddef.h>
#include "resource_occupation.h"

#if DEV_SAADC_MGNT_ENABLED

#include "saadc_mgnt.h"
#include "app_error.h"
#include "adc.h"
#include "main.h"
#include "n32l40x_adc.h"
#include "timer_tools.h"

#ifndef ASSERT
#define ASSERT(EXPR) assert(EXPR)
#endif

#if (DEV_SAADC_MGNT_LOG_ENABLED)&&(DEV_ENABLED)
#include "log.h"
#define SAADC_MGNT_LOG_WARNING(...)               \
	do {                                          \
		(void)LOG_WARNING("ssadc_mgnt", __VA_ARGS__); \
	} while(0)
#define SAADC_MGNT_LOG_INFO(...)               \
	do {                                          \
		(void)LOG_INFO("ssadc_mgnt", __VA_ARGS__); \
	} while(0)
#endif

/* Defines -------------------------------------------------------------------*/
#define SAADC_MGNT_CH_VOLT_REF         3.3f
#define SAADC_MGNT_RESOLUTION          4095.0f
#define SAADC_CTRL_BAT_ENABLE_PIN_ID   4U
#define SAADC_TIMEOUT_US              10000U

/* Private variables ---------------------------------------------------------*/
static bool saadc_enable = false;
static bool saadc_calibrated = false;
static uint32_t mgnt_pins_num = 0;
static saadc_mgnt_pin_t * mgnt_pins = NULL;

/* Private function declarations ---------------------------------------------*/
static bool map_ainx_to_channel(uint32_t ainx, uint8_t *channel)
{
	if(channel == NULL) {
		return false;
	}
	switch(ainx) {
		case 0: *channel = ADC_CH_1_PA0; return true;
		case 1: *channel = ADC_CH_2_PA1; return true;
		case 2: *channel = ADC_CH_3_PA2; return true;
		case 3: *channel = ADC_CH_4_PA3; return true;
		case 4: *channel = ADC_CH_5_PA4; return true;
		case 5: *channel = ADC_CH_6_PA5; return true;
		case 6: *channel = ADC_CH_7_PA6; return true;
		case 7: *channel = ADC_CH_8_PA7; return true;
		case 8: *channel = ADC_CH_9_PB0; return true;
		case 9: *channel = ADC_CH_10_PB1; return true;
		default:
			return false;
	}
}

static bool map_ctrl_pin(uint32_t pin, GPIO_Module **port, uint16_t *gpio_pin)
{
	if((port == NULL) || (gpio_pin == NULL)) {
		return false;
	}
	switch(pin) {
		case SAADC_CTRL_BAT_ENABLE_PIN_ID:
			*port = ADC_Control_GPIO_Port;
			*gpio_pin = ADC_Control_Pin;
			return true;
		default:
			return false;
	}
}

static void mgnt_pin_ctrl(saadc_mgnt_pin_ctrl_t * const _pin_ctrl, bool _active)
{
	GPIO_Module *port = NULL;
	uint16_t gpio_pin = 0;
	bool state;

	if((_pin_ctrl == NULL) || !_pin_ctrl->pin_ctrl_enable) {
		return;
	}
	if(!map_ctrl_pin(_pin_ctrl->pin, &port, &gpio_pin)) {
		return;
	}

	state = _active ? _pin_ctrl->active_level : !_pin_ctrl->active_level;
	GPIO_WriteBit(port, gpio_pin, state ? Bit_SET : Bit_RESET);
}

static uint32_t saadc_timeout_start(void)
{
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
	return DWT->CYCCNT;
}

static bool saadc_has_timed_out(uint32_t start_cycles)
{
	uint32_t timeout_cycles = (uint32_t)(((uint64_t)SystemCoreClock * SAADC_TIMEOUT_US + 999999U) / 1000000U);
	return (uint32_t)(DWT->CYCCNT - start_cycles) >= timeout_cycles;
}

static bool saadc_power_on(void)
{
	ADC_Enable(ADC, ENABLE);
	hw_delay_us(8U);
	uint32_t start_cycles = saadc_timeout_start();
	while(ADC_GetFlagStatusNew(ADC, ADC_FLAG_RDY) == RESET) {
		if(saadc_has_timed_out(start_cycles)) {
			ADC_Enable(ADC, DISABLE);
			return false;
		}
	}
	return true;
}

void saadc_mgnt_init(saadc_mgnt_pin_t * const _mgnt_pins, const uint32_t _num)
{
	ASSERT(_mgnt_pins != NULL);
	ASSERT(_num != 0);

	mgnt_pins = _mgnt_pins;
	mgnt_pins_num = _num;

	if(!adc_is_initialized()) {
		NS_ADC1_Init();
		saadc_calibrated = false;
	}
	if(!saadc_calibrated) {
		if(!saadc_power_on()) {
			APP_ERROR_HANDLER(ERR_TIMEOUT);
			return;
		}
		ADC_StartCalibration(ADC);
		hw_delay_us(8U);
		uint32_t start_cycles = saadc_timeout_start();
		while(ADC_GetCalibrationStatus(ADC) != RESET) {
			if(saadc_has_timed_out(start_cycles)) {
				ADC_Enable(ADC, DISABLE);
				APP_ERROR_HANDLER(ERR_TIMEOUT);
				return;
			}
		}
		ADC_Enable(ADC, DISABLE);
		saadc_calibrated = true;
	}
#if (DEV_SAADC_MGNT_LOG_ENABLED)&&(DEV_ENABLED)
	SAADC_MGNT_LOG_INFO("SAADC mgnt already enabled!");
#endif
	saadc_enable = true;
	saadc_mgnt_disable();
}

void saadc_mgnt_enable(void)
{
	if(saadc_enable) {
		return;
	}
	saadc_enable = true;
	for(uint32_t i = 0; i < mgnt_pins_num; i++) {
		mgnt_pin_ctrl(&mgnt_pins[i].pin_ctrl, true);
	}
}

void saadc_mgnt_disable(void)
{
	if(!saadc_enable) {
		return;
	}
	saadc_enable = false;
	for(uint32_t i = 0; i < mgnt_pins_num; i++) {
		mgnt_pin_ctrl(&mgnt_pins[i].pin_ctrl, false);
	}
}

SYSTEM_ERROR_CODE_E saadc_mgnt_sample_blocking(float * const _volt_arr)
{
	SYSTEM_ERROR_CODE_E err_code = ERR_NONE;

	ASSERT(_volt_arr != NULL);

	if(!saadc_enable || !saadc_calibrated) {
#if (DEV_SAADC_MGNT_LOG_ENABLED)&&(DEV_ENABLED)
	SAADC_MGNT_LOG_WARNING("SAADC mgnt already enabled!");
#endif
		return ERR_INVALID_STATE;
	}

	for(uint32_t i = 0; i < mgnt_pins_num; i++) {
		uint8_t channel = 0;
		uint32_t adc_raw;

		if(!map_ainx_to_channel(mgnt_pins[i].AINx, &channel)) {
			err_code = ERR_INVALID_ARG;
			continue;
		}

		ADC_ConfigRegularChannel(ADC, channel, 1, ADC_SAMP_TIME_239CYCLES5);
		if(!saadc_power_on()) {
			err_code = ERR_TIMEOUT;
			continue;
		}
		ADC_ClearFlag(ADC, ADC_FLAG_ENDCA | ADC_FLAG_ENDC | ADC_FLAG_STR);
		ADC_EnableSoftwareStartConv(ADC, ENABLE);
		uint32_t start_cycles = saadc_timeout_start();
		bool conversion_finished = true;
		while(ADC_GetFlagStatus(ADC, ADC_FLAG_ENDCA) == RESET) {
			if(saadc_has_timed_out(start_cycles)) {
				conversion_finished = false;
				break;
			}
		}
		ADC_EnableSoftwareStartConv(ADC, DISABLE);
		if(!conversion_finished) {
			ADC_Enable(ADC, DISABLE);
			ADC_ClearFlag(ADC, ADC_FLAG_ENDCA | ADC_FLAG_ENDC | ADC_FLAG_STR);
			err_code = ERR_TIMEOUT;
			continue;
		}

		adc_raw = ADC_GetDat(ADC);
		ADC_ClearFlag(ADC, ADC_FLAG_ENDCA | ADC_FLAG_ENDC | ADC_FLAG_STR);
		ADC_Enable(ADC, DISABLE);
#if (DEV_SAADC_MGNT_LOG_ENABLED)&&(DEV_ENABLED)
		SAADC_MGNT_LOG_INFO("SAADC module busy when visit channel: %d", i);
#endif
		_volt_arr[i] = ((float)adc_raw * SAADC_MGNT_CH_VOLT_REF) / SAADC_MGNT_RESOLUTION;
	}

	return err_code;
}

#endif
/**
 * @}
 */
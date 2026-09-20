/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "resource_occupation.h"

#if NRFX_CHECK(DEV_TM1640B_ENABLED)

#include "dev_tm1640b.h"
#include "log.h"
#include "main.h"
#include "timer_tools.h"
#include <string.h>

#if DEV_TM1640B_LOG_ENABLED
#define TM1640B_LOG_WARNING(...) LOG_WARNING("tm1640b", __VA_ARGS__)
#else
#define TM1640B_LOG_WARNING(...) do {} while(0)
#endif
	
/* Defines -------------------------------------------------------------------*/
/* tm1640b support 16 digits connection in maxinum */
#define tm1640b_MAX_DIGITS_SUPPORT		16

#define tm1640b_DATACTRL_CMD		    0x40
#define tm1640b_DISCTRL_CMD			    0x80
#define tm1640b_ADDRCTRL_CMD		    0xC0

#define DATACTRL_ADDR_INC				(0 << 2)
#define DATACTRL_ADDR_FIX				(1 << 2)

#define DISCTRL_DIS_ON					(1 << 3)
#define DISCTRL_DIS_OFF					(0 << 3)

/* tm1640b provides 8 different extinction width */
#define DISCTRL_EXTINCT_WIDTH_BASE		0

/* Provides 16 data registers with base address 0x00 */
#define ADDRCTRL_ADDR_BASE              0

#define TM1640B_CLK_PORT                DIS_SCK_GPIO_Port
#define TM1640B_CLK_PIN                 DIS_SCK_Pin
#define TM1640B_DATA_PORT               DIS_MOSI_GPIO_Port
#define TM1640B_DATA_PIN                DIS_MOSI_Pin

/* Private variables ---------------------------------------------------------*/
static bool in_sleeping = true;
static uint8_t brightness = 0x0B;

/* Private function declarations ---------------------------------------------*/
static void tm1640b_delay_us(uint16_t us);
static void tm1640b_gpio_init(void);
static void tm1640b_clk_write(bool high);
static void tm1640b_data_write(bool high);
static ret_code_t tm1640b_validate_send_args(uint8_t start_addr, uint8_t const *pdata, uint8_t len);
static void tm1640b_send_bytes(uint8_t const *data, uint8_t len);
static ret_code_t tm1640b_send_bytes_checked(uint8_t const *data, uint8_t len);
/* Function prototypes -------------------------------------------------------*/
/**@brief Function Brief.
 *
 * @param[in]   xxx   Parameter description
 * @param[out]  xxx   Parameter description
 */
static void tm1640b_delay_us(uint16_t us)
{
	hw_delay_us(us);
}

static void tm1640b_gpio_init(void)
{
	GPIO_InitType gpio_init = {0};

	RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB, ENABLE);
	hw_delay_tim_init();
	tm1640b_data_write(true);
	tm1640b_clk_write(true);
	gpio_init.Pin = TM1640B_CLK_PIN;
	gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
	gpio_init.GPIO_Pull = GPIO_No_Pull;
	gpio_init.GPIO_Current = GPIO_DC_4mA;
	gpio_init.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
	GPIO_InitPeripheral(TM1640B_CLK_PORT, &gpio_init);
	gpio_init.Pin = TM1640B_DATA_PIN;
	GPIO_InitPeripheral(TM1640B_DATA_PORT, &gpio_init);
}

static void tm1640b_clk_write(bool high)
{
	if(high) {
		GPIO_SetBits(TM1640B_CLK_PORT, TM1640B_CLK_PIN);
	} else {
		GPIO_ResetBits(TM1640B_CLK_PORT, TM1640B_CLK_PIN);
	}
}

static void tm1640b_data_write(bool high)
{
	if(high) {
		GPIO_SetBits(TM1640B_DATA_PORT, TM1640B_DATA_PIN);
	} else {
		GPIO_ResetBits(TM1640B_DATA_PORT, TM1640B_DATA_PIN);
	}
}

static ret_code_t tm1640b_validate_send_args(uint8_t start_addr, uint8_t const *pdata, uint8_t len)
{
	if(pdata == NULL) {
		return NS_ERROR_NULL;
	}
	if(len == 0U) {
		return NS_ERROR_INVALID_LENGTH;
	}
	if(len > tm1640b_MAX_DIGITS_SUPPORT) {
		return NS_ERROR_INVALID_LENGTH;
	}
	if(start_addr >= tm1640b_MAX_DIGITS_SUPPORT) {
		return NS_ERROR_INVALID_PARAM;
	}
	if((uint32_t)start_addr + (uint32_t)len > tm1640b_MAX_DIGITS_SUPPORT) {
		return NS_ERROR_INVALID_LENGTH;
	}
	return NS_SUCCESS;
}

static void tm1640b_send_bytes(uint8_t const *data, uint8_t len)
{
	tm1640b_data_write(false);
	tm1640b_delay_us(2);
	tm1640b_clk_write(false);
	tm1640b_delay_us(2);

	for(uint8_t i = 0; i < len; i++) {
		uint8_t byte = data[i];
		for(uint8_t bit = 0; bit < 8; bit++) {
			tm1640b_data_write((byte & 0x01U) != 0U);
			tm1640b_delay_us(2);
			tm1640b_clk_write(true);
			tm1640b_delay_us(2);
			tm1640b_clk_write(false);
			byte >>= 1;
		}
	}

	tm1640b_data_write(false);
	tm1640b_delay_us(2);
	tm1640b_clk_write(true);
	tm1640b_delay_us(2);
	tm1640b_data_write(true);
	tm1640b_delay_us(2);
}

static ret_code_t tm1640b_send_bytes_checked(uint8_t const *data, uint8_t len)
{
	if(data == NULL) {
		return NS_ERROR_NULL;
	}
	if(len == 0U) {
		return NS_ERROR_INVALID_LENGTH;
	}
	tm1640b_send_bytes(data, len);
	return NS_SUCCESS;
}

ret_code_t dev_tm1640b_sendcmd(uint8_t _cmd) {
	return tm1640b_send_bytes_checked(&_cmd, 1);
}


ret_code_t dev_tm1640b_set_incmode(void) {
	uint8_t cmd = tm1640b_DATACTRL_CMD | DATACTRL_ADDR_INC;
	return dev_tm1640b_sendcmd(cmd);
}


ret_code_t dev_tm1640b_senddata(uint8_t _start_addr, uint8_t const * _pdata, uint8_t _len) {
	if(in_sleeping) {
		return NS_ERROR_INVALID_STATE;
	}

	ret_code_t err_code = tm1640b_validate_send_args(_start_addr, _pdata, _len);
	if(err_code != NS_SUCCESS) {
		TM1640B_LOG_WARNING("invalid senddata args, addr=%u len=%u err=0x%08X", _start_addr, _len, err_code);
		return err_code;
	}

	err_code = dev_tm1640b_set_incmode();
	if(err_code != NS_SUCCESS) {
		return err_code;
	}
	
	static uint8_t tx_buf[tm1640b_MAX_DIGITS_SUPPORT+1];

	tx_buf[0] = tm1640b_ADDRCTRL_CMD | (ADDRCTRL_ADDR_BASE + _start_addr);
	memcpy(tx_buf+1, _pdata, _len);

	return tm1640b_send_bytes_checked(tx_buf, _len + 1U);
}


ret_code_t dev_tm1640b_onoff_ctrl(bool _flag) {
	if(in_sleeping) {
		return NS_ERROR_INVALID_STATE;
	}
	
	uint8_t cmd = 0;
	(_flag)? (cmd = tm1640b_DISCTRL_CMD | DISCTRL_DIS_ON | brightness) : (cmd = tm1640b_DISCTRL_CMD | DISCTRL_DIS_OFF);
	return dev_tm1640b_sendcmd(cmd);
}


void dev_tm1640b_sleeping(void) {
	if(!in_sleeping) {
		in_sleeping = true;
		
		uint8_t cmd = tm1640b_DISCTRL_CMD | DISCTRL_DIS_OFF;
		(void)dev_tm1640b_sendcmd(cmd);
		
		tm1640b_delay_us(10);
		(void)dev_tm1640b_sendcmd(0xA0);
		
		tm1640b_delay_us(100);
		tm1640b_clk_write(true);
		tm1640b_data_write(true);
	}
}


ret_code_t dev_tm1640b_wakeup(void) {
	ret_code_t err_code;
	
	if(in_sleeping) {
		in_sleeping = false;
		tm1640b_gpio_init();
		tm1640b_delay_us(10);
		
		uint8_t cmd = 0x88;
		err_code = dev_tm1640b_sendcmd(cmd);
		if(err_code != NS_SUCCESS) {
			in_sleeping = true;
			return err_code;
		}
	}
	
	return NS_SUCCESS;
}


void dev_tm1640b_init(void) {
	tm1640b_gpio_init();
	tm1640b_delay_us(1);
	
	in_sleeping = false;
	
	(void)dev_tm1640b_onoff_ctrl(false);
}


void dev_tm1640b_setbrightness(uint8_t const _brightness) {
	if(_brightness > DISCTRL_DIS_MAX_BRIGHT) {
		brightness = DISCTRL_DIS_MAX_BRIGHT;
	}
	else if(_brightness < DISCTRL_DIS_MIN_BRIGHT) {
		brightness = DISCTRL_DIS_MIN_BRIGHT;
	}
	else {
		brightness = _brightness;
	}
}


bool dev_tm1640b_in_sleeping(void) {
	return in_sleeping;
}


void dev_tm1640b_sendcore_software(uint8_t * _arr, uint8_t _len) {
	if((_arr == NULL) || (_len == 0U)) {
		return;
	}
	tm1640b_send_bytes(_arr, _len);
}


void dev_tm1640b_set_incmode_software(void) {
	uint8_t cmd = tm1640b_DATACTRL_CMD | DATACTRL_ADDR_INC;
	dev_tm1640b_sendcore_software(&cmd, 1);
}


void dev_tm1640b_senddata_software(uint8_t _start_addr, uint8_t const * _pdata, uint8_t _len) {
	if(tm1640b_validate_send_args(_start_addr, _pdata, _len) != NS_SUCCESS) {
		return;
	}

	dev_tm1640b_set_incmode_software();
	
	static uint8_t tx_buf[tm1640b_MAX_DIGITS_SUPPORT+1];

	tx_buf[0] = tm1640b_ADDRCTRL_CMD | (ADDRCTRL_ADDR_BASE + _start_addr);
	memcpy(tx_buf+1, _pdata, _len);
	
	dev_tm1640b_sendcore_software(tx_buf, _len + 1U);
}


void dev_tm1640b_onoff_ctrl_software(bool _flag) {
	if(in_sleeping) {
		return;
	}
	
	uint8_t cmd = 0;
	(_flag)? (cmd = tm1640b_DISCTRL_CMD | DISCTRL_DIS_ON | brightness) : (cmd = tm1640b_DISCTRL_CMD | DISCTRL_DIS_OFF);
	dev_tm1640b_sendcore_software(&cmd, 1);
}


void dev_tm1640b_wakeup_software(void) {
	if(in_sleeping) {
		in_sleeping = false;
		uint8_t cmd = 0x88;
		dev_tm1640b_sendcore_software(&cmd, 1);
	}
}


void dev_tm1640b_init_software(void) {
	tm1640b_gpio_init();
	tm1640b_delay_us(1);
}
#endif
/**
 * @}
 */
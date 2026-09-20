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

#if NRFX_CHECK(DEV_CS1237_ENABLED)

#include <stdbool.h>
#include "dev_cs1237.h"
#include "main.h"
#include "spi.h"
#include "timer_tools.h"
#include "ns_error.h"
#include "FreeRTOS.h"
#include "task.h"

#if (DEV_CS1237_LOG_ENABLED)&&(DEV_ENABLED)
#include "log.h"
#define CS1237_MGNT_LOG_INFO(...)                  \
	do {                                           \
		(void)LOG_INFO("CS1237_mgnt", __VA_ARGS__); \
	} while(0)
#define CS1237_MGNT_LOG_WARNING(...)               \
	do {                                          \
		(void)LOG_WARNING("CS1237_mgnt", __VA_ARGS__); \
	} while(0)
#else
#define CS1237_MGNT_LOG_INFO(...) do { } while(0)
#define CS1237_MGNT_LOG_WARNING(...) do { } while(0)
#endif

/* Defines -------------------------------------------------------------------*/
#define CS1237_DRDY_PORT          SEN_MISO_GPIO_Port
#define CS1237_DRDY_PIN           SEN_MISO_Pin
#define CS1237_SPI_SCK_PORT       SEN_SCK_GPIO_Port
#define CS1237_SPI_SCK_PIN        SEN_SCK_Pin
#define CS1237_SPI               SPI1
#define CS1237_TIMEOUT_MS         2U
#define CS1237_READY_TIMEOUT_MS   1000U
#define CS1237_POWER_DOWN_HOLD_US 100U
#define CS1237_POWER_UP_HOLD_US   10U
#define CS1237_SETTLE_40HZ_MS     80U
#define CS1237_READ_TAIL_CLOCK_COUNT 3U
#define CS1237_WRITE_CMD          0x65U

#define CS1237_STAGE_IDLE                 0U
#define CS1237_STAGE_INIT_BEGIN           1U
#define CS1237_STAGE_CFG_WAIT_READY       2U
#define CS1237_STAGE_CFG_DUMMY16          3U
#define CS1237_STAGE_CFG_DUMMY13          4U
#define CS1237_STAGE_CFG_WRITE_CMD        5U
#define CS1237_STAGE_CFG_WRITE_DATA       6U
#define CS1237_STAGE_READ_READY_CHECK     7U
#define CS1237_STAGE_READ_DATA_BYTE0      8U
#define CS1237_STAGE_READ_DATA_BYTE1      9U
#define CS1237_STAGE_READ_DATA_BYTE2      10U
#define CS1237_STAGE_READ_TAIL_CLOCK      11U
#define CS1237_STAGE_READ_DONE            12U
#define CS1237_STAGE_WAKEUP_BEGIN         13U
#define CS1237_STAGE_WAKEUP_SETTLING      14U
#define CS1237_STAGE_BYTE_WAIT_TXE        80U
#define CS1237_STAGE_BYTE_WAIT_RXNE       81U
#define CS1237_STAGE_BYTE_WAIT_BSY        82U

#define CS1237_CH_ADC       (0x00)
#define CS1237_PGA_128      (0x03 << 2)
#define CS1237_RATE_40HZ    (0x01 << 4)
#define CS1237_REF_ON       (0x00 << 6)
#define CS1237_CONFIG(pga, rate) ((pga) | (rate) | CS1237_REF_ON | CS1237_CH_ADC)

typedef enum {
	CS1237_STATE_SLEEPING = 0,
	CS1237_STATE_WAIT_DRDY,
	CS1237_STATE_SETTLING,
	CS1237_STATE_READY,
	CS1237_STATE_ERROR
} cs1237_state_t;

typedef enum {
	CS1237_REQUEST_NONE = 0,
	CS1237_REQUEST_WAKEUP,
	CS1237_REQUEST_SLEEP
} cs1237_request_t;

/* Private variables ---------------------------------------------------------*/
volatile int32_t g_cs1237_debug_raw = 0;
static volatile cs1237_state_t cs1237_state = CS1237_STATE_SLEEPING;
static volatile cs1237_request_t cs1237_request = CS1237_REQUEST_NONE;
static TickType_t cs1237_state_tick = 0;
static bool cs1237_configured = false;
volatile uint32_t g_cs1237_last_error = NS_SUCCESS;
volatile uint32_t g_cs1237_debug_stage = CS1237_STAGE_IDLE;
volatile uint32_t g_cs1237_debug_datasize = 0U;
volatile uint32_t g_cs1237_debug_transfer_status = NS_SUCCESS;
volatile uint32_t g_cs1237_debug_drdy = 0U;
volatile uint32_t g_cs1237_debug_spi_sts = 0U;
volatile uint32_t g_cs1237_debug_spi_ctrl2 = 0U;
volatile uint32_t g_cs1237_debug_spi_ctrl1 = 0U;
volatile uint16_t g_cs1237_debug_last_rx_word = 0U;
volatile uint32_t g_cs1237_debug_gpioa_pmode = 0U;
volatile uint32_t g_cs1237_debug_gpioa_afl = 0U;
volatile uint32_t g_cs1237_debug_gpioa_afh = 0U;
volatile uint32_t g_cs1237_debug_gpiob_pmode = 0U;
volatile uint32_t g_cs1237_debug_gpiob_afl = 0U;
volatile uint32_t g_cs1237_dma_frame_count = 0U;
volatile uint32_t g_cs1237_dma_error_count = 0U;
volatile uint32_t g_cs1237_dma_busy_count = 0U;

static dev_cs1237_err_handler_t err_handler = NULL;
static volatile bool sample_dma_active = false;
static volatile uint32_t sample_started_cycles = 0U;
static uint8_t sample_tx[3] = {0};
static volatile uint8_t sample_rx[3] = {0};

/* Private function declarations ---------------------------------------------*/
static void cs1237_record_error(ret_code_t err)
{
	g_cs1237_last_error = (uint32_t)err;
	g_cs1237_debug_transfer_status = (uint32_t)err;
	if((err != NS_SUCCESS) && (err != NS_ERROR_BUSY) && (err_handler != NULL)) {
		err_handler((uint32_t)err);
	}
}

static void cs1237_delay_us(uint16_t us)
{
	hw_delay_us(us);
}

static void cs1237_sck_as_gpio(bool high)
{
	GPIO_InitType gpio = {0};
	RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA, ENABLE);
	if(high) {
		GPIO_SetBits(CS1237_SPI_SCK_PORT, CS1237_SPI_SCK_PIN);
	} else {
		GPIO_ResetBits(CS1237_SPI_SCK_PORT, CS1237_SPI_SCK_PIN);
	}
	gpio.Pin = CS1237_SPI_SCK_PIN;
	gpio.GPIO_Mode = GPIO_Mode_Out_PP;
	gpio.GPIO_Pull = GPIO_No_Pull;
	gpio.GPIO_Current = GPIO_DC_2mA;
	gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
	GPIO_InitPeripheral(CS1237_SPI_SCK_PORT, &gpio);
}

static void cs1237_drdy_irq_disable(void)
{
	EXTI->IMASK &= ~EXTI_LINE6;
	EXTI_ClrITPendBit(EXTI_LINE6);
}

static void cs1237_drdy_irq_enable(void)
{
	EXTI_ClrITPendBit(EXTI_LINE6);
	EXTI->IMASK |= EXTI_LINE6;
}

static void cs1237_dma_stop(void)
{
	SPI_I2S_EnableDma(CS1237_SPI, SPI_I2S_DMA_RX | SPI_I2S_DMA_TX, DISABLE);
	DMA_EnableChannel(DMA_CH1, DISABLE);
	DMA_EnableChannel(DMA_CH2, DISABLE);
	DMA_ClearFlag(DMA_FLAG_GL1 | DMA_FLAG_GL2, DMA);
}

static void cs1237_data_as_input(void)
{
	GPIO_InitType gpio = {0};
	gpio.Pin = CS1237_DRDY_PIN;
	gpio.GPIO_Mode = GPIO_Mode_Input;
	gpio.GPIO_Pull = GPIO_No_Pull;
	gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
	gpio.GPIO_Alternate = GPIO_AF0_SPI1;
	GPIO_InitPeripheral(CS1237_DRDY_PORT, &gpio);
}

static void cs1237_data_as_output(void)
{
	GPIO_InitType gpio = {0};
	GPIO_SetBits(CS1237_DRDY_PORT, CS1237_DRDY_PIN);
	gpio.Pin = CS1237_DRDY_PIN;
	gpio.GPIO_Mode = GPIO_Mode_Out_PP;
	gpio.GPIO_Pull = GPIO_No_Pull;
	gpio.GPIO_Current = GPIO_DC_2mA;
	gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
	GPIO_InitPeripheral(CS1237_DRDY_PORT, &gpio);
}

static void cs1237_spi_write_mode_prepare(void)
{
	cs1237_dma_stop();
	SPI_Enable(CS1237_SPI, DISABLE);
	cs1237_sck_as_gpio(false);
	cs1237_data_as_input();
}

static void cs1237_spi_write_mode_restore(void)
{
	NS_SPI1_Init();
	g_cs1237_debug_datasize = SPI_DATA_SIZE_8BITS;
	g_cs1237_debug_spi_ctrl1 = CS1237_SPI->CTRL1;
	g_cs1237_debug_spi_ctrl2 = CS1237_SPI->CTRL2;
	g_cs1237_debug_spi_sts = CS1237_SPI->STS;
	g_cs1237_debug_gpioa_pmode = GPIOA->PMODE;
	g_cs1237_debug_gpioa_afl = GPIOA->AFL;
	g_cs1237_debug_gpioa_afh = GPIOA->AFH;
	g_cs1237_debug_gpiob_pmode = GPIOB->PMODE;
	g_cs1237_debug_gpiob_afl = GPIOB->AFL;
}

static void cs1237_gpio_clock(void)
{
	cs1237_delay_us(1);
	GPIO_SetBits(CS1237_SPI_SCK_PORT, CS1237_SPI_SCK_PIN);
	cs1237_delay_us(2);
	GPIO_ResetBits(CS1237_SPI_SCK_PORT, CS1237_SPI_SCK_PIN);
	cs1237_delay_us(2);
}

static void cs1237_gpio_write_bit(bool high)
{
	if(high) {
		GPIO_SetBits(CS1237_DRDY_PORT, CS1237_DRDY_PIN);
	} else {
		GPIO_ResetBits(CS1237_DRDY_PORT, CS1237_DRDY_PIN);
	}
	cs1237_gpio_clock();
}

static void cs1237_gpio_write_bits(uint16_t value, uint8_t bit_count)
{
	uint8_t bit_index;

	for(bit_index = 0U; bit_index < bit_count; bit_index++) {
		uint16_t mask = (uint16_t)1U << (bit_count - 1U - bit_index);
		cs1237_gpio_write_bit((value & mask) != 0U);
	}
}

static void cs1237_pulse_tail_clocks(void)
{
	uint8_t clock_index;
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	SPI_Enable(CS1237_SPI, DISABLE);
	CS1237_SPI_SCK_PORT->PBC = CS1237_SPI_SCK_PIN;
	CS1237_SPI_SCK_PORT->PMODE = (CS1237_SPI_SCK_PORT->PMODE & ~GPIO_PMODE5_Msk) | GPIO_PMODE5_1;
	cs1237_delay_us(1);
	for(clock_index = 0U; clock_index < CS1237_READ_TAIL_CLOCK_COUNT; clock_index++) {
		CS1237_SPI_SCK_PORT->PBSC = CS1237_SPI_SCK_PIN;
		cs1237_delay_us(2);
		CS1237_SPI_SCK_PORT->PBC = CS1237_SPI_SCK_PIN;
		cs1237_delay_us(2);
	}
	CS1237_SPI_SCK_PORT->PMODE = (CS1237_SPI_SCK_PORT->PMODE & ~GPIO_PMODE5_Msk) | GPIO_PMODE5_2;
	SPI_Enable(CS1237_SPI, ENABLE);
	__set_PRIMASK(primask);
}


static ret_code_t cs1237_write_config(uint8_t config_mark)
{
	uint8_t clock_index;
	uint32_t primask;

	cs1237_spi_write_mode_prepare();
	/* Preemption with SCK high for >=100 us would power down the ADC. */
	primask = __get_PRIMASK();
	__disable_irq();
	g_cs1237_debug_stage = CS1237_STAGE_CFG_DUMMY16;
	for(clock_index = 0U; clock_index < 16U; clock_index++) {
		cs1237_gpio_clock();
	}
	g_cs1237_debug_stage = CS1237_STAGE_CFG_DUMMY13;
	for(clock_index = 0U; clock_index < 13U; clock_index++) {
		cs1237_gpio_clock();
	}
	cs1237_data_as_output();
	g_cs1237_debug_stage = CS1237_STAGE_CFG_WRITE_CMD;
	cs1237_gpio_write_bits(CS1237_WRITE_CMD, 7U);
	g_cs1237_debug_stage = CS1237_STAGE_CFG_WRITE_DATA;
	cs1237_gpio_clock();
	cs1237_gpio_write_bits(config_mark, 8U);
	cs1237_data_as_input();
	cs1237_gpio_clock();
	__set_PRIMASK(primask);

	cs1237_spi_write_mode_restore();
	g_cs1237_debug_stage = CS1237_STAGE_IDLE;
	return NS_SUCCESS;
}

void dev_cs1237_init(dev_cs1237_err_handler_t _err_handler)
{
	GPIO_InitType gpio = {0};

	err_handler = _err_handler;
	g_cs1237_last_error = NS_SUCCESS;
	g_cs1237_debug_stage = CS1237_STAGE_INIT_BEGIN;
	NS_SPI1_Init();
	cs1237_dma_stop();
	SPI_Enable(CS1237_SPI, DISABLE);
	cs1237_sck_as_gpio(true);
	cs1237_delay_us(150);
	cs1237_state = CS1237_STATE_SLEEPING;
	cs1237_request = CS1237_REQUEST_NONE;
	cs1237_configured = false;
	sample_dma_active = false;
	NVIC_DisableIRQ(EXTI9_5_IRQn);
	GPIO_ConfigEXTILine(GPIOA_PORT_SOURCE, GPIO_PIN_SOURCE6);
	gpio.Pin = CS1237_DRDY_PIN;
	gpio.GPIO_Mode = GPIO_Mode_IT_Falling;
	gpio.GPIO_Pull = GPIO_No_Pull;
	gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
	GPIO_InitPeripheral(CS1237_DRDY_PORT, &gpio);
	cs1237_drdy_irq_disable();
	NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
	NVIC_SetPriority(EXTI9_5_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
	NVIC_EnableIRQ(EXTI9_5_IRQn);
	g_cs1237_debug_stage = CS1237_STAGE_IDLE;
}

ret_code_t dev_cs1237_process(void)
{
	TickType_t now = xTaskGetTickCount();
	ret_code_t err;
	cs1237_request_t request = cs1237_request;

	if(sample_dma_active &&
	   (uint32_t)(DWT->CYCCNT - sample_started_cycles) >=
	   (SystemCoreClock / 1000U) * CS1237_TIMEOUT_MS) {
		uint32_t primask = __get_PRIMASK();
		__disable_irq();
		if(sample_dma_active &&
		   (uint32_t)(DWT->CYCCNT - sample_started_cycles) >=
		   (SystemCoreClock / 1000U) * CS1237_TIMEOUT_MS) {
			dev_cs1237_on_spi_dma_error_isr();
			g_cs1237_last_error = NS_ERROR_TIMEOUT;
			g_cs1237_debug_transfer_status = NS_ERROR_TIMEOUT;
		}
		__set_PRIMASK(primask);
	}

	if(request == CS1237_REQUEST_SLEEP) {
		uint32_t primask = __get_PRIMASK();
		__disable_irq();
		if(sample_dma_active) {
			__set_PRIMASK(primask);
			return NS_ERROR_BUSY;
		}
		cs1237_request = CS1237_REQUEST_NONE;
		cs1237_drdy_irq_disable();
		__set_PRIMASK(primask);
		if(cs1237_state != CS1237_STATE_SLEEPING) {
			cs1237_dma_stop();
			SPI_Enable(CS1237_SPI, DISABLE);
			cs1237_sck_as_gpio(true);
			cs1237_delay_us(CS1237_POWER_DOWN_HOLD_US);
		}
		cs1237_state = CS1237_STATE_SLEEPING;
		g_cs1237_debug_stage = CS1237_STAGE_IDLE;
		return NS_SUCCESS;
	}

	if(request == CS1237_REQUEST_WAKEUP) {
		cs1237_request = CS1237_REQUEST_NONE;
		if(cs1237_state != CS1237_STATE_READY) {
			g_cs1237_debug_stage = CS1237_STAGE_WAKEUP_BEGIN;
			cs1237_sck_as_gpio(false);
			cs1237_delay_us(CS1237_POWER_UP_HOLD_US);
			NS_SPI1_Init();
			cs1237_state_tick = now;
			if(cs1237_configured) {
				cs1237_state = CS1237_STATE_SETTLING;
				g_cs1237_debug_stage = CS1237_STAGE_WAKEUP_SETTLING;
			} else {
				cs1237_state = CS1237_STATE_WAIT_DRDY;
			}
			g_cs1237_last_error = NS_SUCCESS;
		}
	}

	switch(cs1237_state) {
		case CS1237_STATE_WAIT_DRDY:
			g_cs1237_debug_stage = CS1237_STAGE_CFG_WAIT_READY;
			g_cs1237_debug_drdy = GPIO_ReadInputDataBit(CS1237_DRDY_PORT, CS1237_DRDY_PIN);
			if(g_cs1237_debug_drdy != 0U) {
				if((now - cs1237_state_tick) >= pdMS_TO_TICKS(CS1237_READY_TIMEOUT_MS)) {
					cs1237_state = CS1237_STATE_ERROR;
					cs1237_record_error(NS_ERROR_TIMEOUT);
					CS1237_MGNT_LOG_WARNING("CS1237 timeout waiting for DRDY");
					return NS_ERROR_TIMEOUT;
				}
				return NS_ERROR_BUSY;
			}

			err = cs1237_write_config(CS1237_CONFIG(CS1237_PGA_128, CS1237_RATE_40HZ));
			if(err != NS_SUCCESS) {
				cs1237_state = CS1237_STATE_ERROR;
				cs1237_record_error(err);
				return err;
			}
			cs1237_configured = true;
			cs1237_state = CS1237_STATE_SETTLING;
			cs1237_state_tick = now;
			g_cs1237_debug_stage = CS1237_STAGE_WAKEUP_SETTLING;
			return NS_ERROR_BUSY;

		case CS1237_STATE_SETTLING:
			if((now - cs1237_state_tick) < pdMS_TO_TICKS(CS1237_SETTLE_40HZ_MS)) {
				return NS_ERROR_BUSY;
			}
			cs1237_state = CS1237_STATE_READY;
			g_cs1237_debug_stage = CS1237_STAGE_IDLE;
			cs1237_record_error(NS_SUCCESS);
			return dev_cs1237_acquisition_resume();

		case CS1237_STATE_READY:
			return NS_SUCCESS;

		case CS1237_STATE_ERROR:
			return (ret_code_t)g_cs1237_last_error;

		case CS1237_STATE_SLEEPING:
		default:
			return NS_ERROR_BUSY;
	}
}

bool dev_cs1237_is_ready(void)
{
	return cs1237_state == CS1237_STATE_READY;
}

ret_code_t dev_cs1237_acquisition_pause(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	if(cs1237_state != CS1237_STATE_READY) {
		__set_PRIMASK(primask);
		return NS_ERROR_INVALID_STATE;
	}
	if(sample_dma_active) {
		__set_PRIMASK(primask);
		return NS_ERROR_BUSY;
	}

	cs1237_drdy_irq_disable();
	__set_PRIMASK(primask);
	return NS_SUCCESS;
}

ret_code_t dev_cs1237_acquisition_resume(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	if(cs1237_state != CS1237_STATE_READY) {
		__set_PRIMASK(primask);
		return NS_ERROR_INVALID_STATE;
	}

	if(!sample_dma_active) {
		cs1237_drdy_irq_enable();
		if(GPIO_ReadInputDataBit(CS1237_DRDY_PORT, CS1237_DRDY_PIN) == 0U) {
			dev_cs1237_on_drdy_falling_isr();
		}
	}
	__set_PRIMASK(primask);
	return NS_SUCCESS;
}

uint32_t dev_cs1237_sample_count(void)
{
	return g_cs1237_dma_frame_count;
}

bool dev_cs1237_read_sample(int32_t *sample, uint32_t *sample_count)
{
	uint32_t primask;

	if((sample == NULL) || (sample_count == NULL)) {
		return false;
	}

	primask = __get_PRIMASK();
	__disable_irq();
	*sample = g_cs1237_debug_raw;
	*sample_count = g_cs1237_dma_frame_count;
	__set_PRIMASK(primask);
	return true;
}

void dev_cs1237_on_drdy_falling_isr(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	if(cs1237_state != CS1237_STATE_READY) {
		__set_PRIMASK(primask);
		return;
	}
	if(sample_dma_active) {
		g_cs1237_dma_busy_count++;
		__set_PRIMASK(primask);
		return;
	}

	cs1237_drdy_irq_disable();
	sample_dma_active = true;
	g_cs1237_debug_stage = CS1237_STAGE_READ_DATA_BYTE0;
	cs1237_dma_stop();
	DMA_CH1->MADDR = (uint32_t)sample_rx;
	DMA_SetCurrDataCounter(DMA_CH1, sizeof(sample_rx));
	DMA_CH2->MADDR = (uint32_t)sample_tx;
	DMA_SetCurrDataCounter(DMA_CH2, sizeof(sample_tx));
	sample_started_cycles = DWT->CYCCNT;
	__DMB();
	SPI_I2S_EnableDma(CS1237_SPI, SPI_I2S_DMA_RX | SPI_I2S_DMA_TX, ENABLE);
	DMA_EnableChannel(DMA_CH1, ENABLE);
	SPI_Enable(CS1237_SPI, ENABLE);
	DMA_EnableChannel(DMA_CH2, ENABLE);
	__set_PRIMASK(primask);
}

void dev_cs1237_on_spi_txrx_complete_isr(void)
{
	uint32_t raw_data;
	uint32_t started = DWT->CYCCNT;
	uint32_t timeout_cycles = (SystemCoreClock / 1000U) * CS1237_TIMEOUT_MS;
	int32_t sample;

	if(!sample_dma_active) {
		return;
	}

	while(((CS1237_SPI->STS & SPI_I2S_BUSY_FLAG) != 0U) &&
		  ((uint32_t)(DWT->CYCCNT - started) < timeout_cycles)) {
		__NOP();
	}
	cs1237_dma_stop();
	if((CS1237_SPI->STS & (SPI_I2S_BUSY_FLAG | SPI_MODERR_FLAG | SPI_I2S_OVER_FLAG)) != 0U) {
		dev_cs1237_on_spi_dma_error_isr();
		return;
	}
	__DMB();
	raw_data = ((uint32_t)sample_rx[0] << 16) |
			   ((uint32_t)sample_rx[1] << 8) |
			   (uint32_t)sample_rx[2];
	sample = ((int32_t)(raw_data << 8)) >> 8;
	g_cs1237_debug_stage = CS1237_STAGE_READ_TAIL_CLOCK;
	cs1237_pulse_tail_clocks();
	g_cs1237_debug_raw = sample;
	g_cs1237_debug_last_rx_word = sample_rx[2];
	g_cs1237_dma_frame_count++;
	g_cs1237_debug_transfer_status = NS_SUCCESS;
	g_cs1237_last_error = NS_SUCCESS;
	g_cs1237_debug_stage = CS1237_STAGE_READ_DONE;
	sample_dma_active = false;
	cs1237_drdy_irq_enable();
}

void dev_cs1237_on_spi_dma_error_isr(void)
{
	cs1237_drdy_irq_disable();
	cs1237_dma_stop();
	SPI_Enable(CS1237_SPI, DISABLE);
	cs1237_sck_as_gpio(false);
	sample_dma_active = false;
	g_cs1237_dma_error_count++;
	g_cs1237_last_error = NS_ERROR_INTERNAL;
	g_cs1237_debug_transfer_status = NS_ERROR_INTERNAL;
	cs1237_state = CS1237_STATE_ERROR;
}

void EXTI9_5_IRQHandler(void)
{
	if(EXTI_GetITStatus(EXTI_LINE6) != RESET) {
		EXTI_ClrITPendBit(EXTI_LINE6);
		dev_cs1237_on_drdy_falling_isr();
	}
}

void DMA_Channel1_IRQHandler(void)
{
	if(DMA_GetFlagStatus(DMA_FLAG_TE1, DMA) != RESET ||
	   DMA_GetFlagStatus(DMA_FLAG_TE2, DMA) != RESET) {
		dev_cs1237_on_spi_dma_error_isr();
	} else if(DMA_GetIntStatus(DMA_INT_TXC1, DMA) != RESET) {
		DMA_ClrIntPendingBit(DMA_INT_TXC1, DMA);
		dev_cs1237_on_spi_txrx_complete_isr();
	}
}

void DMA_Channel2_IRQHandler(void)
{
	if(DMA_GetIntStatus(DMA_INT_ERR2, DMA) != RESET) {
		dev_cs1237_on_spi_dma_error_isr();
	}
}

void dev_cs1237_read_result(int32_t * _res)
{
	if(_res == NULL) {
		return;
	}

	*_res = g_cs1237_debug_raw;
}

void dev_cs1237_sleeping(void)
{
	cs1237_request = CS1237_REQUEST_SLEEP;
}

ret_code_t dev_cs1237_wakeup(void)
{
	if(cs1237_state == CS1237_STATE_READY) {
		return NS_SUCCESS;
	}

	if((cs1237_state == CS1237_STATE_WAIT_DRDY) ||
	   (cs1237_state == CS1237_STATE_SETTLING)) {
		return NS_ERROR_BUSY;
	}

	if(cs1237_request == CS1237_REQUEST_SLEEP) {
		return NS_ERROR_BUSY;
	}
	cs1237_request = CS1237_REQUEST_WAKEUP;
	return NS_SUCCESS;
}

bool dev_cs1237_is_sleeping(void)
{
	return cs1237_state == CS1237_STATE_SLEEPING;
}

bool dev_cs1237_transfer_active(void)
{
	return sample_dma_active;
}
#endif
/**
 * @}
 */
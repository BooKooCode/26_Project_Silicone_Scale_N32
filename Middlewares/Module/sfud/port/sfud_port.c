/*
 * This file is part of the Serial Flash Universal Driver Library.
 *
 * Copyright (c) 2016-2018, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Portable interface for each platform.
 * Created on: 2016-04-23
 */
/* Includes ------------------------------------------------------------------*/
#include "resource_occupation.h"

#if DEV_SFUD_ENABLED

#include <sfud.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#include <FreeRTOS.h>
#include <task.h>
#include "semphr.h"
#include "log.h"
#include "spi.h"
#include "main.h"
#include "timer_tools.h"

#if (DEV_ENABLED && DEV_SFUD_LOG_ENABLED)
#define SFUD_PORT_LOG_DEBUG(...) do { (void)LOG_DEBUG("sfud", __VA_ARGS__); } while (0)
#else
#define SFUD_PORT_LOG_DEBUG(...) do { } while (0)
#endif

/* Defines -------------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
static user_data_spix_t user_data_spix = {
	.instance = SPI2,
};

static SemaphoreHandle_t sfud_mutex = NULL;
static StaticSemaphore_t sfud_mutex_buffer;

#if (DEV_ENABLED && DEV_SFUD_LOG_ENABLED)
static char log_buf[192];
#endif


/* Private function declarations ---------------------------------------------*/
void sfud_log_debug(const char *file, const long line, const char *format, ...);

/* Function prototypes -------------------------------------------------------*/
/**@brief Retry function used when a SPI FLASH operation failed (eg. write request in busy)
 *
 * @param[in]   None
 * @param[out]  None
 * @return			None
 */
void retry_delay_1ms(void) {
	/*
		When NRF_ERROR handler occurs, 
		RTC1 interrput will be shield (which acts as the FreeRTOS systick),
		thus the blocking delay function was necessary
	*/
	if(xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
		TickType_t delay_ticks = pdMS_TO_TICKS(1);
		vTaskDelay(delay_ticks > 0U ? delay_ticks : 1U);
	} else {
		hw_delay_us(1000U);
	}
}


static sfud_err spi_write_read(	const sfud_spi *spi, const uint8_t *write_buf, size_t write_size, uint8_t *read_buf, size_t read_size) {
	(void)spi;
	switch(spi_flash_write_read(write_buf, write_size, read_buf, read_size)) {
		case SPI_FLASH_OK: return SFUD_SUCCESS;
		case SPI_FLASH_READ_ERROR: return SFUD_ERR_READ;
		default: return SFUD_ERR_WRITE;
	}
}


static void spi_lock(const sfud_spi *spi) {
	(void)spi;
	if(sfud_mutex != NULL) {
        (void)xSemaphoreTake(sfud_mutex, portMAX_DELAY);
	}
}


static void spi_unlock(const sfud_spi *spi) {
	(void)spi;
	if(sfud_mutex != NULL) {
        (void)xSemaphoreGive(sfud_mutex);
	}
}


sfud_err sfud_spi_port_init(sfud_flash *flash) {
	sfud_err result = SFUD_SUCCESS;

	/**
	 * add your port spi bus and device object initialize code like this:
	 * 1. rcc initialize
	 * 2. gpio initialize
	 * 3. spi device initialize
	 * 4. flash->spi and flash->retry item initialize
	 *    flash->spi.wr = spi_write_read; //Required
	 *    flash->spi.qspi_read = qspi_read; //Required when QSPI mode enable
	 *    flash->spi.lock = spi_lock;
	 *    flash->spi.unlock = spi_unlock;
	 *    flash->spi.user_data = &spix;
	 *    flash->retry.delay = null;
	 *    flash->retry.times = 10000; //Required
	 */
	/* User data structure initialize */
	if(flash == NULL) {
		return SFUD_ERR_NOT_FOUND;
	}

	spi_flash_init();
	
	/* FLASH structure initialize */
	flash->spi.wr = spi_write_read;
	flash->spi.lock = spi_lock;
	flash->spi.unlock = spi_unlock;
	flash->spi.user_data = &user_data_spix;
	
	/*
		Delay function and retry times are used in FLASH chip busy situation.
	*/
	flash->retry.delay = retry_delay_1ms;
	
	/* Timeout: (retry.times) * 1ms */
	flash->retry.times = 500;

	if(sfud_mutex == NULL) {
		sfud_mutex = xSemaphoreCreateMutexStatic(&sfud_mutex_buffer);
		if(sfud_mutex == NULL) {
			return SFUD_ERR_NOT_FOUND;
		}
	}

	return result;
}

/**
 * This function is print debug info.
 *
 * @param file the file which has call this function
 * @param line the line number which has call this function
 * @param format output format
 * @param ... args
 */
void sfud_log_debug(const char *file, const long line, const char *format, ...) {
#if (DEV_ENABLED && DEV_SFUD_LOG_ENABLED)
	va_list args;
	/* args point to the first variable parameter */
	va_start(args, format);
	SFUD_PORT_LOG_DEBUG("in %s:%ld ", file, line);
	/* must use vprintf to print */
	vsnprintf(log_buf, sizeof(log_buf), format, args);
	SFUD_PORT_LOG_DEBUG("%s", log_buf);
	va_end(args);
#endif
}

/**
 * This function is print routine info.
 *
 * @param format output format
 * @param ... args
 */
void sfud_log_info(const char *format, ...) {
#if (DEV_ENABLED && DEV_SFUD_LOG_ENABLED)
    va_list args;
    /* args point to the first variable parameter */
    va_start(args, format);
    /* must use vprintf to print */
    vsnprintf(log_buf, sizeof(log_buf), format, args);
	SFUD_PORT_LOG_DEBUG("%s", log_buf);
    va_end(args);
#endif
}

#endif
/**
 * @}
 */

#ifndef __DEV_CS1237_H__
#define __DEV_CS1237_H__

#ifdef __cplusplus
extern "C" {
#endif
	
#include <stdbool.h>
#include <stdint.h>
#include "ns_error.h"
	
/**
 * @defgroup dev_cs1237
 * @{
 * @ingroup dev_cs1237
 * @brief   cs1237 driver depends on SPIM peripheral.
 */

/**
 * @brief CS1237 error code handler, see @ref nrfx_spim_xfer for details of the _err_code
 */
typedef void (*dev_cs1237_err_handler_t)(uint32_t _err_code);

extern volatile int32_t g_cs1237_debug_raw;
extern volatile uint32_t g_cs1237_last_error;
extern volatile uint32_t g_cs1237_debug_stage;
extern volatile uint32_t g_cs1237_dma_frame_count;
extern volatile uint32_t g_cs1237_dma_error_count;
extern volatile uint32_t g_cs1237_dma_busy_count;

/**
 * @brief Function for CS1237 initialize.
 *
 * @retval None.
 */
void dev_cs1237_init(dev_cs1237_err_handler_t _err_handler);

/**
 * @brief Advance the non-blocking wakeup state machine.
 *
 * @retval NS_SUCCESS The device is ready.
 * @retval NS_ERROR_BUSY Wakeup is still in progress or the device is sleeping.
 * @retval Others Wakeup failed.
 */
ret_code_t dev_cs1237_process(void);

/**
 * @brief Check whether the device is ready for acquisition.
 */
bool dev_cs1237_is_ready(void);

ret_code_t dev_cs1237_acquisition_pause(void);
ret_code_t dev_cs1237_acquisition_resume(void);
uint32_t dev_cs1237_sample_count(void);
bool dev_cs1237_read_sample(int32_t *sample, uint32_t *sample_count);

void dev_cs1237_on_drdy_falling_isr(void);
void dev_cs1237_on_spi_txrx_complete_isr(void);
void dev_cs1237_on_spi_dma_error_isr(void);

/**
 * @brief Function for reading the result of cs1237.
 *
 * @retval None.
 */
void dev_cs1237_read_result(int32_t * _res);

/**
 * @brief Function for setting the cs1237 into sleep.
 *
 * @retval None.
 */
void dev_cs1237_sleeping(void);

/**
 * @brief Enter CS1237 Power-down synchronously.
 *
 * SCLK is driven high for longer than the datasheet minimum of 100 us.
 *
 * @retval NS_SUCCESS Power-down timing has completed.
 * @retval NS_ERROR_BUSY A DMA transfer is still active; retry after it completes.
 */
ret_code_t dev_cs1237_power_down(void);

/** Select the conversion rate used on the next wake from Power-down. */
void dev_cs1237_set_probe_rate(bool fast);
	
/**
 * @brief Start waking the CS1237 without waiting for completion.
 *
 * @retval NS_SUCCESS Wakeup was accepted or the device is already ready.
 */
ret_code_t dev_cs1237_wakeup(void);
bool dev_cs1237_is_sleeping(void);
bool dev_cs1237_transfer_active(void);


/** @} */

#ifdef __cplusplus
}
#endif

#endif /* DEV_CS1237_H__ */

#ifndef __DEV_BUZZER_H__
#define __DEV_BUZZER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
/**
 * @defgroup nrf_dev_buzzer
 * @{
 * @ingroup nrf_dev_buzzer
 * @brief   PWM based buzzer.
 */

/**
 * @brief Enumeration for the buzzer event.
 */
enum {
	BUZZER_EVT_NONE = 0U,
	BUZZER_EVT_PLAYDONE,
};

/**
 * @brief Enumeration for registered buzzer songs.
 */
typedef enum _buzzer_songidx_s{
	SONG_STARTUP = 0U,
	SONG_PRESSBTN,
    SONG_INCHARGE,
	
	SONG_NUMS
} buzzer_songidx_s;
	
typedef void (*dev_buzzer_evt_handler_t)(uint32_t _evt);

typedef dev_buzzer_evt_handler_t buzzer_evt_handler_t;

/**
 * @brief Function for buzzer initialize.
 *
 * @retval None.
 */
void dev_buzzer_init(dev_buzzer_evt_handler_t _h);

/**
 * @brief Function for changing the buzzer voice.
 *
 * @retval None.
 */
void dev_buzzer_changelevel(uint8_t const _level);

/**
 * @brief Function for requesting a buzzer action defined in the @enum buzzer_songidx_s
 *
 * @retval None.
 */
void dev_buzzer_action(buzzer_songidx_s _idx);

uint8_t dev_buzzer_getlevel(void);

void dev_buzzer_enable(void);

void dev_buzzer_disable(void);

void dev_buzzer_on_tim_elapsed(void);

/**
 * @brief Check if the buzzer is currently playing a melody.
 * @retval true  if a melody is in progress.
 * @retval false if the buzzer is idle.
 */
bool dev_buzzer_is_playing(void);


/** @} */

#ifdef __cplusplus
}
#endif

#endif	// NRF_DEV_BUZZER_H__

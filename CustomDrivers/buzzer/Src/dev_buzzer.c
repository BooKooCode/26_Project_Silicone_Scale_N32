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

#if NRFX_CHECK(DEV_BUZZER_ENABLED)

#include <stdbool.h>
#include "dev_buzzer.h"
#include "app_error.h"
#include "tim.h"
#include "timer_tools.h"
#include "main.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "task.h"

#if (DEV_SFUD_LOG_ENABLED)&&(DEV_ENABLED)
#include "log.h"
#define SFUD_MGNT_LOG_WARNING(...)               \
	do {                                          \
		(void)LOG_WARNING("sfud_mgnt", __VA_ARGS__); \
	} while(0)
#endif

/* Defines -------------------------------------------------------------------*/
typedef struct _songprop_s {
	uint32_t beats_permin;
	uint32_t tone_num;
	const uint8_t * tone_tab;
	const uint8_t * beats_tab;
} songprop_s;

#define BUZZER_PWM_TIM_CLOCK_HZ 1000000U
#define BUZZER_PWM_CHANNEL      TIM_CH_1
#define BUZZER_SONG_TAIL_GUARD_MS 2U
#define BUZZER_INTER_NOTE_GAP_MIN_MS 3U
#define BUZZER_INTER_NOTE_GAP_MAX_MS 10U
#define BUZZER_INTER_NOTE_GAP_DIVISOR 10U

/* Private variables ---------------------------------------------------------*/

/* Startup melody */
const uint8_t startup_tone_tab[] = {0,1,5,4,8};
const uint8_t startup_beats_tab[] = {2,2,2,2,2};
const songprop_s startup_prop = {
	.beats_permin = 190, \
	.tone_num = sizeof(startup_tone_tab)/sizeof(uint8_t), \
	.tone_tab = startup_tone_tab, \
	.beats_tab = startup_beats_tab, \
};

/* Click melody */
const uint8_t pressbtn_tone_tab[] = {8};
const uint8_t pressbtn_beats_tab[] = {1};
const songprop_s pressbtn_prop = {
	.beats_permin = 240, \
	.tone_num = sizeof(pressbtn_tone_tab)/sizeof(uint8_t), \
	.tone_tab = pressbtn_tone_tab, \
	.beats_tab = pressbtn_beats_tab, \
};

/* Incharge melody */
const uint8_t incharge_tone_tab[] = {10};
const uint8_t incharge_beats_tab[] = {1};
const songprop_s incharge_prop = {
	.beats_permin = 150, \
	.tone_num = sizeof(incharge_tone_tab)/sizeof(uint8_t), \
	.tone_tab = incharge_tone_tab, \
	.beats_tab = incharge_beats_tab, \
};


static songprop_s const * songlist[SONG_NUMS] = {
	&startup_prop, &pressbtn_prop, &incharge_prop
};

const uint32_t midtone_frq_tab[10] = {
	1046, 1174, 1318, 1396, 1567, 1760, 1975, 2092, 4184, 4435
};

static bool pwm_started = false;
static bool pwm_initialized = false;
static bool tone_timer_initialized = false;
static bool buzzer_enabled = false;

static songprop_s const * cur_playsong;
static volatile uint32_t cur_tone_idx = 0;
static volatile bool in_playsong = false;
static volatile bool tail_guard_pending = false;
static volatile bool inter_note_gap_pending = false;

static uint8_t tone_level = 100;

static volatile uint32_t buzzer_event = BUZZER_EVT_NONE;
static dev_buzzer_evt_handler_t evt_handler = NULL;

static void buzzer_finish_song(bool from_isr);
static void buzzer_play_tone(uint8_t tone, uint8_t beats);

static void buzzer_evt_handler_deferred(void * arg1, uint32_t arg2)
{
	(void)arg1;
	if(evt_handler != NULL) {
		evt_handler(arg2);
	}
}

static void buzzer_tone_timer_prepare(void)
{
	if(!tone_timer_initialized) {
		NS_TIM6_Init();
		tone_timer_initialized = true;
	}
}

static void buzzer_tone_timer_stop(void)
{
	buzzer_tone_timer_prepare();
	TIM_Enable(TIM6, DISABLE);
	TIM_ConfigInt(TIM6, TIM_INT_UPDATE, DISABLE);
	TIM_SetCnt(TIM6, 0U);
	TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
}

static void buzzer_tone_timer_start(uint32_t duration_ms)
{
	buzzer_tone_timer_prepare();

	if(duration_ms == 0U) {
		duration_ms = 1U;
	} else if(duration_ms > 0x10000UL) {
		duration_ms = 0x10000UL;
	}

	TIM_Enable(TIM6, DISABLE);
	TIM_ConfigInt(TIM6, TIM_INT_UPDATE, DISABLE);
	TIM_SetAutoReload(TIM6, (uint16_t)(duration_ms - 1U));
	TIM_GenerateEvent(TIM6, TIM_EVT_SRC_UPDATE);
	TIM_SetCnt(TIM6, 0U);
	TIM_ClrIntPendingBit(TIM6, TIM_INT_UPDATE);
	TIM_ConfigInt(TIM6, TIM_INT_UPDATE, ENABLE);
	TIM_Enable(TIM6, ENABLE);
}

/* Private function declarations ---------------------------------------------*/
static void buzzer_pwm_pin_config(bool alternate)
{
	GPIO_InitType gpio = {0};
#if (DEV_BUZZER_ACTIVE_LEVEL == 1)
	GPIO_ResetBits(BEEP_GPIO_Port, BEEP_Pin);
#else
	GPIO_SetBits(BEEP_GPIO_Port, BEEP_Pin);
#endif
	gpio.Pin = BEEP_Pin;
	gpio.GPIO_Mode = alternate ? GPIO_Mode_AF_PP : GPIO_Mode_Out_PP;
	gpio.GPIO_Alternate = GPIO_AF2_TIM1;
	gpio.GPIO_Pull = GPIO_No_Pull;
	gpio.GPIO_Current = GPIO_DC_2mA;
	gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
	GPIO_InitPeripheral(BEEP_GPIO_Port, &gpio);
}

static void buzzer_pwm_set_silence(void)
{
#if (DEV_BUZZER_ACTIVE_LEVEL == 1)
	TIM_SetCmp1(TIM1, 0U);
#else
	TIM_SetAutoReload(TIM1, 999U);
	TIM_SetCmp1(TIM1, 1000U);
#endif
}

static void buzzer_pwm_prepare(void)
{
	if(!pwm_initialized) {
		NS_TIM1_Init();
		pwm_initialized = true;
	}
	buzzer_pwm_set_silence();
	if(!pwm_started) {
		TIM_SetCnt(TIM1, 0U);
		TIM_GenerateEvent(TIM1, TIM_EVT_SRC_UPDATE);
		TIM_EnableCapCmpCh(TIM1, BUZZER_PWM_CHANNEL, TIM_CAP_CMP_ENABLE);
		TIM_EnableCtrlPwmOutputs(TIM1, ENABLE);
		TIM_Enable(TIM1, ENABLE);
		buzzer_pwm_pin_config(true);
		pwm_started = true;
	}
}

static void buzzer_delay_ms(uint32_t duration_ms)
{
	while(duration_ms > 0U) {
		hw_delay_us(1000U);
		duration_ms--;
	}
}

static uint32_t buzzer_note_duration_ms(uint8_t beats)
{
	uint32_t denominator = cur_playsong->beats_permin * 4UL;
	uint32_t dur = ((60000UL * (uint32_t)beats) + (denominator / 2UL)) / denominator;
	return (dur == 0U) ? 1U : dur;
}

static bool buzzer_note_need_gap(uint32_t tone_idx)
{
	if((cur_playsong == NULL) || ((tone_idx + 1U) >= cur_playsong->tone_num)) {
		return false;
	}

	if((cur_playsong->tone_tab[tone_idx] == 0U) || (cur_playsong->tone_tab[tone_idx + 1U] == 0U) || (tone_level == 0U)) {
		return false;
	}

	return true;
}

static uint32_t buzzer_note_gap_ms(uint32_t tone_idx)
{
	uint32_t total_ms;
	uint32_t gap_ms;

	if(!buzzer_note_need_gap(tone_idx)) {
		return 0U;
	}

	total_ms = buzzer_note_duration_ms(cur_playsong->beats_tab[tone_idx]);
	gap_ms = total_ms / BUZZER_INTER_NOTE_GAP_DIVISOR;

	if(gap_ms < BUZZER_INTER_NOTE_GAP_MIN_MS) {
		gap_ms = BUZZER_INTER_NOTE_GAP_MIN_MS;
	} else if(gap_ms > BUZZER_INTER_NOTE_GAP_MAX_MS) {
		gap_ms = BUZZER_INTER_NOTE_GAP_MAX_MS;
	}

	if(gap_ms >= total_ms) {
		gap_ms = (total_ms > 1U) ? (total_ms - 1U) : 0U;
	}

	return gap_ms;
}

static uint32_t buzzer_note_sound_ms(uint32_t tone_idx)
{
	uint32_t total_ms = buzzer_note_duration_ms(cur_playsong->beats_tab[tone_idx]);
	uint32_t gap_ms = buzzer_note_gap_ms(tone_idx);

	return total_ms - gap_ms;
}

static void buzzer_start_current_tone(void)
{
	buzzer_play_tone(cur_playsong->tone_tab[cur_tone_idx], cur_playsong->beats_tab[cur_tone_idx]);
	buzzer_tone_timer_start(buzzer_note_sound_ms(cur_tone_idx));
}

static void buzzer_play_tone(uint8_t tone, uint8_t beats)
{
	uint32_t top_val;
	uint32_t pulse;

	ASSERT(beats != 0);
	ASSERT(cur_playsong != NULL);

	buzzer_pwm_prepare();
	TIM_Enable(TIM1, DISABLE);

	if((tone == 0U) || (tone_level == 0U)) {
		top_val = BUZZER_PWM_TIM_CLOCK_HZ / 1000U;
		TIM_SetAutoReload(TIM1, (uint16_t)(top_val - 1U));
		buzzer_pwm_set_silence();
	} else {
		uint32_t freq = midtone_frq_tab[tone - 1U];
		top_val = BUZZER_PWM_TIM_CLOCK_HZ / freq;
		if(top_val < 2U) {
			top_val = 2U;
		}
		TIM_SetAutoReload(TIM1, (uint16_t)(top_val - 1U));

		/* Map volume to max 50% duty to keep buzzer timbre acceptable. */
		pulse = (top_val * (uint32_t)tone_level) / 200U;
		if(pulse >= top_val) {
			pulse = top_val - 1U;
		}
#if (DEV_BUZZER_ACTIVE_LEVEL == 1)
		TIM_SetCmp1(TIM1, (uint16_t)pulse);
#else
		TIM_SetCmp1(TIM1, (uint16_t)(top_val - pulse));
#endif
	}
	TIM_SetCnt(TIM1, 0U);
	TIM_GenerateEvent(TIM1, TIM_EVT_SRC_UPDATE);
	TIM_ClrIntPendingBit(TIM1, TIM_INT_UPDATE);
	TIM_Enable(TIM1, ENABLE);
}

static void buzzer_finish_song(bool from_isr)
{
	uint32_t event = buzzer_event;

	buzzer_tone_timer_stop();
	buzzer_pwm_set_silence();
	in_playsong = false;
	cur_tone_idx = 0;
	tail_guard_pending = false;
	inter_note_gap_pending = false;
	buzzer_event = BUZZER_EVT_NONE;

	if((evt_handler != NULL) && (event != BUZZER_EVT_NONE)) {
		if(from_isr) {
			BaseType_t higher_priority_task_woken = pdFALSE;

			if(xTimerPendFunctionCallFromISR(buzzer_evt_handler_deferred, NULL, event,
										 &higher_priority_task_woken) == pdPASS) {
				portYIELD_FROM_ISR(higher_priority_task_woken);
			}
		} else {
			evt_handler(event);
		}
	}
}

void dev_buzzer_on_tim_elapsed(void)
{
	if((!in_playsong) || (cur_playsong == NULL)) {
		tail_guard_pending = false;
		inter_note_gap_pending = false;
		buzzer_tone_timer_stop();
		return;
	}

	if(inter_note_gap_pending) {
		inter_note_gap_pending = false;
		cur_tone_idx++;
		buzzer_start_current_tone();
		return;
	}

	if((cur_tone_idx + 1U) >= cur_playsong->tone_num) {
		if((!tail_guard_pending) && (cur_playsong->tone_tab[cur_tone_idx] != 0U) && (tone_level > 0U)) {
			tail_guard_pending = true;
			buzzer_tone_timer_start(BUZZER_SONG_TAIL_GUARD_MS);
			return;
		}
		buzzer_finish_song(true);
		return;
	}

	tail_guard_pending = false;
	if(buzzer_note_need_gap(cur_tone_idx)) {
		inter_note_gap_pending = true;
		buzzer_pwm_set_silence();
		buzzer_tone_timer_start(buzzer_note_gap_ms(cur_tone_idx));
		return;
	}

	cur_tone_idx++;
	buzzer_start_current_tone();
}

void dev_buzzer_init(dev_buzzer_evt_handler_t _h) {
	evt_handler = _h;
	buzzer_enabled = true;
	buzzer_pwm_prepare();
	tail_guard_pending = false;
	inter_note_gap_pending = false;
	buzzer_tone_timer_stop();

}


void dev_buzzer_changelevel(uint8_t const _level) {
	if(_level >= 0 && _level <= 100) {
		tone_level = _level;
	}
}


uint8_t dev_buzzer_getlevel(void) {
	return tone_level;
}


void dev_buzzer_action(buzzer_songidx_s _idx) {
	ASSERT(_idx < SONG_NUMS);
	/* Must slience when tone_level has been set to 0 */
	if((false == in_playsong) && (tone_level > 0) && buzzer_enabled) {
		cur_playsong = songlist[_idx];
		cur_tone_idx = 0;
        tail_guard_pending = false;
        inter_note_gap_pending = false;
        buzzer_event = BUZZER_EVT_PLAYDONE;

		if(cur_playsong->tone_num >= 1) {
			in_playsong = true;
			if(xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
                for(uint32_t i = 0; i < cur_playsong->tone_num; i++) {
                    uint32_t sound_ms = buzzer_note_sound_ms(i);
                    uint32_t gap_ms = buzzer_note_gap_ms(i);

                    buzzer_play_tone(cur_playsong->tone_tab[i], cur_playsong->beats_tab[i]);
					buzzer_delay_ms(sound_ms);
                    if(gap_ms > 0U) {
                    	buzzer_pwm_set_silence();
                    	buzzer_delay_ms(gap_ms);
                    }
                }
				buzzer_finish_song(false);
            } else {
				buzzer_start_current_tone();
            }
		}
	}
}


void dev_buzzer_enable(void) {
    buzzer_enabled = true;
    buzzer_pwm_prepare();
}


void dev_buzzer_disable(void) {
    buzzer_enabled = false;
    in_playsong = false;
	tail_guard_pending = false;
	inter_note_gap_pending = false;
	buzzer_tone_timer_stop();
    buzzer_pwm_set_silence();
    if(pwm_started) {
		buzzer_pwm_pin_config(false);
		TIM_EnableCtrlPwmOutputs(TIM1, DISABLE);
		TIM_EnableCapCmpCh(TIM1, BUZZER_PWM_CHANNEL, TIM_CAP_CMP_DISABLE);
		TIM_Enable(TIM1, DISABLE);
        pwm_started = false;
    }
}

bool dev_buzzer_is_playing(void)
{
	return in_playsong;
}


#endif
/**
 * @}
 */
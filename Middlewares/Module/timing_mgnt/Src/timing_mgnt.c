/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "timers.h"

#include "./timing_mgnt/timing_mgnt.h"

#include "bookoo_error_def.h"

/* Defines -------------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/**< Polling timer id. */
static TimerHandle_t m_timing_timer_id = NULL;

/* Timing ticks */
static uint32_t timing_ticks = 0;
static uint32_t timing_cnt_incstep = 0;
static uint32_t timing_cnt_limit = 0;

/* Indicate if the timer is stopped */
static bool timing_timer_stop = true;

/* Timing count */
static uint32_t timing_cnt = 0;

/* Timing state */
static timing_mgnt_state_e state;
/* Private function declarations ---------------------------------------------*/
/* Function prototypes -------------------------------------------------------*/
/**@brief Function Brief.
 *
 * @param[in]   xxx   Parameter description
 * @param[out]  xxx   Parameter description
 */
static void timing_timeout_handler(void * p_context)
{
	/* Check if the timer has been stopped */
	if(timing_timer_stop){
		return;
	}
	
	/* Limited the ticks */
	if(timing_cnt <= timing_cnt_limit){
		timing_cnt += timing_cnt_incstep;
	}
	else;
}

void timing_mgnt_init(uint32_t _ticks, uint32_t _cnt_incstep, uint32_t _cnt_limit)
{
	ASSERT(_ticks != 0);
	ASSERT(_cnt_incstep != 0);
	ASSERT((_cnt_limit != 0) && (_cnt_limit >= _cnt_incstep));
	
	timing_ticks = _ticks;
	timing_cnt_incstep = _cnt_incstep;
	timing_cnt_limit = _cnt_limit;
	
	timing_timer_stop = true;
	state = TIMING_RESETED;
	
	/* Create timing counter */
	m_timing_timer_id = xTimerCreate("TIM.Timing",
																(TickType_t)timing_ticks,
																pdTRUE,
																NULL,
																timing_timeout_handler);
	if(m_timing_timer_id == NULL){
		APP_ERROR_HANDLER(BOOKOO_ERROR_TIMING_FAIL);
	}
}

void timing_mgnt_req(timing_mgnt_req_e _req)
{
	switch(_req){
		case TIMING_START_REQ: {
			if(state == TIMING_RESETED){
				timing_timer_stop = false;
				state = TIMING_GOING;
				if(xTimerStart(m_timing_timer_id, 0) != pdPASS){
					APP_ERROR_HANDLER(BOOKOO_ERROR_TIMING_FAIL);
				}
			}
		} break;
		
		case TIMING_STOP_REQ: {
			if(state == TIMING_GOING){
				timing_timer_stop = true;
				state = TIMING_STOPPED;
				if(xTimerStop(m_timing_timer_id, 0) != pdPASS){
					APP_ERROR_HANDLER(BOOKOO_ERROR_TIMING_FAIL);
				}
			}
		} break;
		
		case TIMING_RESET_REQ: {
			if(state == TIMING_STOPPED){
				timing_cnt = 0;
				state = TIMING_RESETED;
			}
		} break;
		
		case TIMING_FORCE_RESET_REQ: {
			if(!timing_timer_stop){
				timing_timer_stop = true;
				if(xTimerStop(m_timing_timer_id, 0) != pdPASS){
					APP_ERROR_HANDLER(BOOKOO_ERROR_TIMING_FAIL);
				}
			}
			state = TIMING_RESETED;
			timing_cnt = 0;
		} break;
		
		default: break;
	};
}

uint32_t * timing_mgnt_get_pcount(void)
{
	return &timing_cnt;
}

timing_mgnt_state_e timing_mgnt_getstate(void)
{
	return state;
}

void timing_mgnt_set_initcount(uint32_t const _count)
{
	if(state == TIMING_RESETED){
		timing_cnt = _count;
	}
}
/**
 * @}
 */

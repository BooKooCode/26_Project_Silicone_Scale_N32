#include "os_mgnt.h"
#include "scale_mass_mgnt.h"
#include "scale_digits_display.h"
#include "dev_status.h"
#include "dev_config.h"
#include "mass_meas.h"
#include "sleep_probe.h"


static mass_mgnt_datapack_s s_mass_pack = {0};
static mass_mgnt_datapack_s s_flowrate_pack = {0};
static os_task_info_t *s_mass_os = NULL;
static sleep_probe_t s_sleep_probe = {0};
typedef enum {
	MASS_PROBE_PHASE_IDLE = 0,
	MASS_PROBE_PHASE_WAKING,
	MASS_PROBE_PHASE_SAMPLING,
	MASS_PROBE_PHASE_POWERING_DOWN,
	MASS_PROBE_PHASE_POWERED_DOWN,
	MASS_PROBE_PHASE_RESTORING_NORMAL,
} mass_probe_phase_e;

static volatile bool s_probe_start_requested = false;
static volatile bool s_probe_stop_requested = false;
static volatile bool s_probe_paused = false;
static volatile bool s_probe_zero_baseline_reset_requested = false;
static volatile bool s_probe_wake_now_requested = false;
static volatile bool s_explicit_wake_startup_requested = false;
static volatile bool s_probe_baseline_valid = false;
static volatile float s_probe_baseline_g = 0.0f;
static volatile mass_probe_phase_e s_probe_phase = MASS_PROBE_PHASE_IDLE;
static bool s_probe_window_driver_error = false;
static bool s_probe_window_pending = false;
static TickType_t s_probe_window_started_at = 0U;
static TickType_t s_probe_powered_down_at = 0U;
static uint32_t s_probe_window_sample_count = 0U;

static void mass_sync_results(void)
{
	dev_var_set(MASS_AFTER_PEEL_INDEX, (FORMAT_4BYTES_U *)&s_mass_pack.quat_after_peeling);
	dev_var_set(MASS_ABSOLUTE_INDEX, (FORMAT_4BYTES_U *)&s_mass_pack.absolute_mass);
	dev_var_set(FLOWRATE_RAW_INDEX, (FORMAT_4BYTES_U *)&s_flowrate_pack.raw);
	dev_var_set(FLOWRATE_QUANTIFIED_INDEX, (FORMAT_4BYTES_U *)&s_flowrate_pack.quantified);
	dev_var_set(FLOWRATE_QUANTIFIED1_INDEX, (FORMAT_4BYTES_U *)&s_flowrate_pack.quantified_1);
}

static void mass_probe_send_event(sleep_probe_event_e event)
{
	uint8_t request;

	switch(event) {
		case SLEEP_PROBE_EVENT_CUP_WAKE:
			request = SYS_CUP_WAKEUP_REQ;
			s_probe_paused = true;
			break;
		case SLEEP_PROBE_EVENT_ERROR_WAKE:
			request = SYS_SLEEP_PROBE_ERROR_REQ;
			break;
		case SLEEP_PROBE_EVENT_RECOVERED:
			request = SYS_SLEEP_PROBE_RECOVERED_REQ;
			break;
		default:
			return;
	}
	(void)send_fsm_critical_request(request);
}

static void mass_probe_apply_requests(void)
{
	taskENTER_CRITICAL();
	if(s_probe_stop_requested) {
		s_probe_stop_requested = false;
		s_probe_start_requested = false;
		s_probe_window_pending = false;
		s_probe_phase = MASS_PROBE_PHASE_RESTORING_NORMAL;
	}
	if(s_probe_start_requested) {
		sleep_probe_init(&s_sleep_probe, s_probe_baseline_valid, s_probe_baseline_g);
		s_probe_start_requested = false;
		s_probe_window_driver_error = false;
		s_probe_window_pending = false;
		s_probe_phase = s_probe_paused ? MASS_PROBE_PHASE_POWERING_DOWN :
										 MASS_PROBE_PHASE_WAKING;
	}
	if(s_probe_zero_baseline_reset_requested) {
		sleep_probe_init(&s_sleep_probe, true, 0.0f);
		s_probe_zero_baseline_reset_requested = false;
		s_probe_window_driver_error = false;
		s_probe_window_pending = false;
		s_probe_phase = s_probe_paused ? MASS_PROBE_PHASE_POWERING_DOWN :
										 MASS_PROBE_PHASE_WAKING;
	}
	if(s_probe_paused &&
	   ((s_probe_phase == MASS_PROBE_PHASE_WAKING) ||
		(s_probe_phase == MASS_PROBE_PHASE_SAMPLING))) {
		s_probe_window_pending = false;
		s_probe_phase = MASS_PROBE_PHASE_POWERING_DOWN;
	}
	if(s_probe_wake_now_requested) {
		s_probe_wake_now_requested = false;
		if(s_probe_phase == MASS_PROBE_PHASE_POWERED_DOWN) {
			s_probe_phase = MASS_PROBE_PHASE_WAKING;
		}
	}
	if(s_explicit_wake_startup_requested) {
		s_probe_window_pending = false;
		s_probe_phase = MASS_PROBE_PHASE_RESTORING_NORMAL;
	}
	taskEXIT_CRITICAL();
}

static void mass_probe_process(void)
{
	ret_code_t status;
	TickType_t now = xTaskGetTickCount();

	switch(s_probe_phase) {
		case MASS_PROBE_PHASE_WAKING:
			if(s_probe_paused) {
				s_probe_phase = MASS_PROBE_PHASE_POWERING_DOWN;
				break;
			}
			status = scale_mass_mgnt_probe_wakeup();
			if(status == NS_SUCCESS) {
				s_probe_window_driver_error = false;
				s_probe_window_pending = true;
				s_probe_window_sample_count = scale_mass_mgnt_sample_count();
				s_probe_window_started_at = now;
				s_probe_phase = MASS_PROBE_PHASE_SAMPLING;
			}
			else if(status != NS_ERROR_BUSY) {
				s_probe_window_driver_error = true;
				s_probe_window_pending = true;
				s_probe_phase = MASS_PROBE_PHASE_POWERING_DOWN;
			}
			break;

		case MASS_PROBE_PHASE_SAMPLING:
			if(s_probe_paused) {
				s_probe_window_pending = false;
				s_probe_phase = MASS_PROBE_PHASE_POWERING_DOWN;
				break;
			}
			status = scale_mass_mgnt_probe_processing();
			if((status != NS_SUCCESS) && (status != NS_ERROR_BUSY)) {
				s_probe_window_driver_error = true;
				s_probe_phase = MASS_PROBE_PHASE_POWERING_DOWN;
			}
			mass_sync_results();
			if((now - s_probe_window_started_at) >= pdMS_TO_TICKS(SLEEP_PROBE_WINDOW_MS)) {
				s_probe_phase = MASS_PROBE_PHASE_POWERING_DOWN;
			}
			break;

		case MASS_PROBE_PHASE_POWERING_DOWN:
			status = scale_mass_mgnt_probe_power_down();
			if(status == NS_ERROR_BUSY) {
				break;
			}
			if(status != NS_SUCCESS) {
				s_probe_window_driver_error = true;
				break;
			}
			if(s_probe_window_pending) {
				mass_probe_send_event(sleep_probe_finish_window(
					&s_sleep_probe,
					scale_mass_mgnt_sample_count() != s_probe_window_sample_count,
					s_probe_window_driver_error,
					scale_mass_mgnt_stableget(),
					s_mass_pack.absolute_mass));
			}
			s_probe_window_pending = false;
			s_probe_powered_down_at = now;
			s_probe_phase = MASS_PROBE_PHASE_POWERED_DOWN;
			break;

		case MASS_PROBE_PHASE_POWERED_DOWN:
			if(!s_probe_paused &&
			   ((now - s_probe_powered_down_at) >=
				pdMS_TO_TICKS(SLEEP_PROBE_PERIOD_MS - SLEEP_PROBE_WINDOW_MS))) {
				s_probe_phase = MASS_PROBE_PHASE_WAKING;
			}
			break;

		case MASS_PROBE_PHASE_RESTORING_NORMAL:
			if(s_explicit_wake_startup_requested && scale_mass_mgnt_normal_ready()) {
				status = scale_mass_mgnt_explicit_wake_startup();
				if(status == NS_ERROR_BUSY) {
					break;
				}
				s_explicit_wake_startup_requested = false;
				s_probe_paused = false;
				s_probe_phase = MASS_PROBE_PHASE_IDLE;
				break;
			}
			status = scale_mass_mgnt_probe_restore_normal();
			if(status != NS_SUCCESS) {
				break;
			}
			if(s_explicit_wake_startup_requested) {
				status = scale_mass_mgnt_explicit_wake_startup();
				if(status == NS_ERROR_BUSY) {
					break;
				}
				s_explicit_wake_startup_requested = false;
				s_probe_paused = false;
			}
			s_probe_phase = MASS_PROBE_PHASE_IDLE;
			break;

		case MASS_PROBE_PHASE_IDLE:
		default:
			break;
	}
}

void mass_sleep_probe_start(void)
{
	taskENTER_CRITICAL();
	s_probe_baseline_valid = true;
	s_probe_baseline_g = 0.0f;
	s_probe_paused = false;
	s_probe_start_requested = true;
	taskEXIT_CRITICAL();
	if(s_mass_os != NULL) {
		xTaskNotifyGive(s_mass_os->handler);
	}
}

void mass_sleep_probe_stop(void)
{
	s_probe_stop_requested = true;
	if(s_mass_os != NULL) {
		xTaskNotifyGive(s_mass_os->handler);
	}
}

void mass_sleep_probe_pause(bool paused)
{
	s_probe_paused = paused;
	if(!paused) {
		s_probe_wake_now_requested = true;
	}
	if(s_mass_os != NULL) {
		xTaskNotifyGive(s_mass_os->handler);
	}
}

void mass_sleep_probe_reset_zero_baseline(void)
{
	s_probe_zero_baseline_reset_requested = true;
	if(s_mass_os != NULL) {
		xTaskNotifyGive(s_mass_os->handler);
	}
}

bool mass_sleep_probe_active(void)
{
	return s_probe_phase != MASS_PROBE_PHASE_IDLE;
}

bool mass_sleep_probe_normal_ready(void)
{
	return (s_probe_phase == MASS_PROBE_PHASE_IDLE) &&
		   scale_mass_mgnt_normal_ready();
}

void mass_explicit_wake_startup(void)
{
	taskENTER_CRITICAL();
	s_explicit_wake_startup_requested = true;
	taskEXIT_CRITICAL();
	if(s_mass_os != NULL) {
		xTaskNotifyGive(s_mass_os->handler);
	}
}


void mass_task(void * argument) {
    os_task_info_t *_task_info = (os_task_info_t *)argument;
    s_mass_os = &_task_info[OS_TASK_MASS];
	/* Pre-Load for task */
	_task_info->wake_tick = xTaskGetTickCount();
	/* Mass manager initialize */
    float _cali_k = 0.0f;
    uint8_t _sensor_type = 0;
    uint8_t _smooth_enable = 0;
    get_kv_param_value(PARAM_KEY_MASS_CALI_K, &_cali_k);
    get_kv_param_value(PARAM_KEY_SENSOR_TYPE, &_sensor_type);
    get_kv_param_value(PARAM_KEY_SMOOTH_ENABLE, &_smooth_enable);
	scale_mass_mgnt_init(_cali_k, \
                            (float)TASK_MASS_INTERVAL_MS / 1000.0f, \
                            &s_flowrate_pack, &s_mass_pack, \
                            _sensor_type);
	scale_mass_mgnt_flowratesmooth(_smooth_enable);
	/* Halting the task at the first time since the low-power consumption request */
	vTaskSuspend(NULL);
    s_mass_os->wake_tick = xTaskGetTickCount();
	
	/* Infinite loop */
	for(;;) {
		TickType_t delay_ticks = pdMS_TO_TICKS(TASK_MASS_INTERVAL_MS);
		mass_probe_apply_requests();
		if(s_probe_phase != MASS_PROBE_PHASE_IDLE) {
			mass_probe_process();
		}
		if(s_probe_phase == MASS_PROBE_PHASE_POWERED_DOWN) {
			TickType_t powered_down_ticks = pdMS_TO_TICKS(
				SLEEP_PROBE_PERIOD_MS - SLEEP_PROBE_WINDOW_MS);
			TickType_t elapsed_ticks = xTaskGetTickCount() - s_probe_powered_down_at;
			if(elapsed_ticks < powered_down_ticks) {
				delay_ticks = powered_down_ticks - elapsed_ticks;
			}
		}
		else if(s_probe_phase == MASS_PROBE_PHASE_SAMPLING) {
			delay_ticks = pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);
		}
		(void)ulTaskNotifyTake(pdTRUE, delay_ticks);
		mass_probe_apply_requests();

		if(s_probe_phase != MASS_PROBE_PHASE_IDLE) {
			mass_probe_process();
			continue;
		}
		
		/* Mass manager processing */
		scale_mass_mgnt_processing();
		
        /* Sync measure result */
		mass_sync_results();

		/* When HALT request was processed, halting the task itself */
		if(scale_mass_mgnt_stateget() == HALT) {
			scale_mass_mgnt_processing();
			vTaskSuspend(NULL);
            s_mass_os->wake_tick = xTaskGetTickCount();
		}
		else if(scale_mass_mgnt_stateget() == MASS_ACQ) {
			digits_disp_req(DISP_EXITBUSY_REQ);
			digits_disp_req(DISP_EXITPEELING_REQ);
			
            uint32_t _peeling_ts = 0;
            dev_status_get(WAIT_PEELING_TIME, &_peeling_ts);
			/* Exit extraction start peeling */
			if(_peeling_ts > 0) {
				dev_status_set(WAIT_PEELING_TIME, (_peeling_ts - 1));
			}
		}
		else;
	}
}



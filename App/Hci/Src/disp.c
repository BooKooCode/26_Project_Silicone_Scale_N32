#include "internal.h"
#include "os_mgnt.h"
#include "dev_status.h"
#include "dev_config.h"
#include "dev_name.h"
#include "timing.h"
#include "scale_digits_display.h"
#include "disp.h"


static os_task_info_t *s_disp_os = NULL;


static void prepare_disp_var(void) {
    FORMAT_4BYTES_U _power;
    FORMAT_4BYTES_U _mass;
    FORMAT_4BYTES_U _flowrate_q1;
    FORMAT_4BYTES_U auto_mass;
    FORMAT_4BYTES_U auto_flowrate;
    FORMAT_4BYTES_U auto_timing_ms;
    
    dev_var_get(POWER_PERCENT_INDEX, &_power);
    dev_var_get(MASS_AFTER_PEEL_INDEX, (FORMAT_4BYTES_U *)&_mass);
    dev_var_get(FLOWRATE_QUANTIFIED1_INDEX, (FORMAT_4BYTES_U *)&_flowrate_q1);
    dev_var_get(AUTO_MASS_INDEX, &auto_mass);
    dev_var_get(AUTO_FLOWRATE_INDEX, &auto_flowrate);
    dev_var_get(AUTO_TIMING_MS_INDEX, &auto_timing_ms);
    digits_disp_update_power(_power.us8[0]);
    digits_disp_update_vars(_mass.fl32, _flowrate_q1.fl32, timing_mgnt_get_pcount());
    digits_disp_update_autovars(auto_mass.fl32, auto_flowrate.fl32, auto_timing_ms.us32);
}


void disp_task(void * argument) {
    os_task_info_t *_task_info = (os_task_info_t *)argument;
    s_disp_os = &_task_info[OS_TASK_DISP];
	/* Digits initialize */
    uint16_t animation_cycle = 0;
    get_kv_param_value(PARAM_KEY_ANI_CYCLE, &animation_cycle);
	digits_disp_init(TASK_DISP_INTERVAL_MS, animation_cycle);
	
	s_disp_os->wake_tick = xTaskGetTickCount();
	/* Halting the task at the first time since the low-power consumption request */
	vTaskSuspend(NULL);
    s_disp_os->wake_tick = xTaskGetTickCount();
    
    uint32_t _loop = 0;
	/* Infinite loop */
	for(;;) {
		vTaskDelayUntil(&s_disp_os->wake_tick, pdMS_TO_TICKS(TASK_DISP_INTERVAL_MS));
		
        if(!(_loop % 3)) {
            /* Update display value */
            prepare_disp_var();
        }
		/* Processing */
		digits_disp_processing();
		
		/* When HALT request was processed, halting the task itself */
		if(digits_disp_stateget() == DISP_HALT) {
            dev_status_set(BUTTON_LOCK_STATE, false);
            uint8_t _req = SLEEPING;
            send_queue_belong_task(OS_TASK_FSM, &_req);
			vTaskSuspend(NULL);
            s_disp_os->wake_tick = xTaskGetTickCount();
		}
        
        _loop ++;
	}
}
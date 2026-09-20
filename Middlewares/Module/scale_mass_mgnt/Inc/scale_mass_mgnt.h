#ifndef SCALE_MASS_MGNT_H__
#define SCALE_MASS_MGNT_H__

#ifdef __cplusplus
extern "C" {
#endif
	
#include <stdint.h>
#include <stdbool.h>
#include "ns_error.h"

/* Statistical window size */


/* Default sensor hyperparameters */
#define MASS_MGNT_PARAMS_DEFAULT            0


typedef enum {
    MASS_STANDARD_100G = 0,
    MASS_STANDARD_500G,
    MASS_STANDARD_1KG,
    
    MASS_STANDARD_WEIGHT_COUNT
} MASS_STANDARD_WEIGHT_TABLE;
    

/* Structure for mass manager datapack */
typedef struct _mass_mgnt_datapack_s{
	/* Calibrated mass data  */
	float raw;
	
	/* Mass data after filtering from the raw one */
	float filtered;
	
	/* Quantification result */
	float quantified;
	float quantified_1;
	
	/* Quantification result after peeling */
	float quat_after_peeling;
	float quat_after_peeling_oz;
	float absolute_mass;
} mass_mgnt_datapack_s;

/* Enumeration for manager request */
typedef enum _mass_mgnt_req_e{
	/* Halting request */
	HALT_REQ = 0U,
	
	/* Startup request */
	STARTUP_REQ,
	
	/* Ratio calibration request */
	CALI_K_REQ,
	CALI_K_EXIT_REQ,
	
	/* Peeling request */
	PEELING_REQ,
	PEELING_EXIT_REQ,
	
	/* Directly peeling request */
	DIRECTLY_PEELING_REQ,
	
}mass_mgnt_req_e;

/* Enumeration for manager states */
typedef enum _mass_mgnt_state_e{
	/* In halt */
	HALT = 0U,
	
	/* In startup (offset calibration) */
	STARTUP,
	
	/* In mass acquisition */
	MASS_ACQ,
	
	/* In ratio calibration */
	CALI_K_WAITING,
	CALI_K_READYPULL,
	CALI_K_PULLWAITING,
	CALI_K_SUCCESS,
	
	/* In peeling */
	PEELING,
	
}mass_mgnt_state_e;

void scale_mass_mgnt_init(const float _cali_k, 
                            const float _Ts_s,
                            mass_mgnt_datapack_s * const _flowrate, 
                            mass_mgnt_datapack_s * const _mass,
                            const uint32_t _params_idx);
void scale_mass_mgnt_processing(void);
ret_code_t scale_mass_mgnt_probe_processing(void);
ret_code_t scale_mass_mgnt_probe_pause(void);
ret_code_t scale_mass_mgnt_probe_resume(void);
ret_code_t scale_mass_mgnt_probe_wakeup(void);
ret_code_t scale_mass_mgnt_probe_power_down(void);
ret_code_t scale_mass_mgnt_probe_restore_normal(void);
bool scale_mass_mgnt_stop_blocked(void);
bool scale_mass_mgnt_normal_ready(void);
uint32_t scale_mass_mgnt_sample_count(void);
ret_code_t scale_mass_mgnt_explicit_wake_startup(void);
void scale_mass_mgnt_req(mass_mgnt_req_e const _req);
mass_mgnt_state_e scale_mass_mgnt_stateget(void);
float scale_mass_mgnt_caliKget(void);
bool scale_mass_mgnt_stableget(void);
void scale_mass_mgnt_flowratesmooth(bool _enable);
void scale_mass_mgnt_switchsensor(const uint32_t _params_idx);
ret_code_t scale_mass_mgnt_filter_config_set(float cutoff_hz, uint32_t window);
void scale_mass_mgnt_filter_config_get(float *cutoff_hz, uint32_t *window);
bool is_mass_standby_stable(void);
void scale_mass_mgnt_history_mass_get(float *_mass_g);
#ifdef __cplusplus
}
#endif

#endif /* SCALE_MASS_MGNT_H__ */

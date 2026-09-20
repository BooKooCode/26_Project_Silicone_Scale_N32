#ifndef SCALE_DIGITS_DISPLAY_H__
#define SCALE_DIGITS_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Enumeration for digits display request */
typedef enum _digits_disp_req_e{
	/* Halting request */
	DISP_HALT_REQ = 0U,
	
	/* Startup request */
	DISP_STARTUP_REQ,
	
	/* Weighting request */
	DISP_WEIGHTING_REQ,
	
	/* Timing request */
	DISP_TIMING_REQ,

	/* Auto mode request */
	DISP_AUTOREADY_REQ,
	DISP_AUTOGOING_REQ,
	DISP_AUTOEND_REQ,
	
	/* Ratio calibration request */
	DISP_CALIK_WAITING_REQ,
	DISP_CALIK_PULL_REQ,
	DISP_CALIK_SUCCESS_REQ,

	/*back set key request*/
	DISP_OZ_MODE_REQ,
	DISP_G_MODE_REQ,
	
	/* Reserved requests */
	DISP_RESERVED_REQ_0,
	DISP_RESERVED_REQ_1,

	DISP_RESERVED_REQ_2,
	DISP_RESERVED_REQ_3,
	
	/* VOICE request */
	DISP_VOICE_ENABLE_REQ,
	DISP_VOICE_DISABLE_REQ,
	
	/* Left button pressed */
	DISP_LEFTPRESS_REQ,
	DISP_LEFTRELEASE_REQ,
	
	/* Right button pressed */
	DISP_RIGHTPRESS_REQ,
	DISP_RIGHTRELEASE_REQ,
	
	/* Low power warning */
	DISP_LOWPOWER_INTOWARNING_REQ,
	DISP_LOWPOWER_EXITWARNING_REQ,
	
	/* Low power protection */
	DISP_LOWPOWER_PROTECT_REQ,
	
	/* In busy indication */
	DISP_INTOBUSY_REQ,
	DISP_EXITBUSY_REQ,
	
	DISP_INCHARGING_REQ,
	DISP_INFULLCHARGING_REQ,
    DISP_TESTLED_REQ,
	DISP_EXITCHARGING_REQ,
	
	DISP_PEELING_REQ,
	DISP_EXITPEELING_REQ,
	
    DISP_RESERVED_REQ_4,
}digits_disp_req_e;

/* Enumeration for manager states */
typedef enum _digits_disp_state_e{
	/* In halt */
	DISP_HALT = 0U,
	
	/* In startup (offset calibration) */
	DISP_STARTUP,
	
	/* Modes */
	DISP_WEIGHTING,
	DISP_TIMING,
	DISP_AUTOREADY,
	DISP_AUTOGOING,
	DISP_AUTOEND,
	
	/* In ratio calibration */
	DISP_CALIK_WAITING,
	DISP_CALIK_PULL,
	DISP_CALIK_SUCCESS,
	
	DISP_LOWPOWER_PROTECT,
	DISP_LOWPOWER_PROTECT_WAITING,
	
	DISP_BYEBYE,
	
	DISP_INCHARGING,
    DISP_TESTLED,
	DISP_EXITCHARGING,
	
	DISP_FINISHSTARTUP,

	DISP_CALIK_WAITING_ANIDONE,
	DISP_RECOVERABLE_ERROR,
    
    DISP_RESERVED_STATE_0,
	
}digits_disp_state_e;


void digits_disp_init(uint32_t const _freq_ms, uint16_t _ani_cycle_ms);
void digits_disp_update_power(uint8_t _power);
void digits_disp_update_vars(float _mass, float _flowrate, uint32_t _timing);
void digits_disp_update_autovars(float _mass, float _flowrate, uint32_t _timing);


void digits_disp_processing(void);
void digits_disp_req(digits_disp_req_e const _req);
void digits_disp_autowelcome(bool _enable);
digits_disp_state_e digits_disp_stateget(void);
void digits_disp_errorcode(uint16_t _err_code);
void digits_disp_recoverable_error(uint16_t error_code);
void digits_disp_enterDFU_software(void);

void digits_disp_getpower_finishcharging(uint8_t const _p);


#ifdef __cplusplus
}
#endif

#endif	/* SCALE_DIGITS_DISPLAY_H__ */

/** @file
 *
 * @defgroup scale_digits_display.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "scale_digits_display.h"
#include <math.h>
#include <string.h>
#include "app_error.h"
#include "dev_tm1640b.h"
#include "main.h"

#include "bookoo_error_def.h"

/* Defines -------------------------------------------------------------------*/
#define DIGIT_POINT_ON		0x80
#define DIGIT_POINT_OFF		0x00
#define DIGIT_TOTAL_NUMS	15
#define DIGIT_NUMERIC_COUNT	11

#define DIGIT_TIMING_START_IDX		0
#define DIGIT_FLOWRATE_START_IDX	3
#define DIGIT_MASS_START_IDX		6

#define DIGIT_MID_MASS_START_IDX	1

#define INDICATOR_H_GRID_IDX		11
#define INDICATOR_J_GRID_IDX		12

#define INDICATOR_H1_LEFT_KEY	0x01
#define INDICATOR_H2_BUSY_1		0x02
#define INDICATOR_H3_BUSY_2		0x04
#define INDICATOR_H4_BUSY_3		0x08
#define INDICATOR_H5_MINUTE		0x10
#define INDICATOR_H6_AUTO		0x20
#define INDICATOR_H7_SECOND		0x40
#define INDICATOR_H8_FLOW		0x80

#define INDICATOR_J1_MUTE		0x01
#define INDICATOR_J2_OZ			0x02
#define INDICATOR_J3_G			0x04
#define INDICATOR_J6_RIGHT_KEY	0x20

#define DISPLAY_MASS_DIGIT_G     	4
#define DISPLAY_MASS_DIGIT_OZ		3
#define DISPLAY_MASS_UP_G			3000.1f
#define DISPLAY_MASS_DOWN_G			-999.9f
#define DISPLAY_MASS_UP_OZ			105.83f
#define DISPLAY_MASS_DOWN_OZ		-35.27f
#define DISPLAY_FLOWRATE_UP			99.9f
#define DISPLAY_TIMING_UP_MS		599000

/* Private variables ---------------------------------------------------------*/
enum {
	PA_POWERON = 0U,
	PA_CALIK_WAITPULL,
	PA_CALIK_PULL,
	PA_CALIK_SUCCESS,
	PA_AUTO_ENTER,
	PA_AUTO_READY,
	PA_LOWPOWER_PROTECT,
	PA_INCHARGING,
	PA_INFULLCHARGING,
	DISP_PATTERN_NUMS,
};


static bool oz_mode = true;
static bool _voice_enable = true;


/* Buffer for digits display */
static uint8_t buf[DIGIT_TOTAL_NUMS] = {0};

/* Pointer to displayed variables */
static float s_power = 0.0f;
static float s_mass, s_flowrate = 0.0f;
static uint32_t s_timing = 0;
static float s_auto_mass, s_auto_flowrate = 0.0f;
static uint32_t s_auto_timing = 0;

/* Current display_state */
static digits_disp_state_e display_state;

/* Counter for animation */
static uint32_t ani_cnt = 0;
static uint32_t ani_state = 0;

/* Display auto mode welcome animation */
static bool disp_auto_welcome = true;

/* Display busy indication */
static uint8_t disp_busy_flags = 0x01;
static bool disp_busy_indication = false;

//
static bool in_fullcharging = false;
static uint8_t charging_redLEDs_spd_cnt = 0;

//
static uint32_t freq_ms = 100;

static uint16_t ani_cycle_ms = 80;

//
static bool disp_peeling_indication = false;

//
static bool disp_halt_batlevel = false;
static uint16_t recoverable_error_code = 0U;


/* Digits encoding */
const uint8_t digits_encoding_tab[16] = {
	0x3F,  //"0"
	0x06,  //"1"
	0x5B,  //"2"
	0x4F,  //"3"
	0x66,  //"4"
	0x6D,  //"5"
	0x7D,  //"6"
	0x07,  //"7"
	0x7F,  //"8"
	0x6F,  //"9"
	0x77,  //"A"
	0x7C,  //"B"
	0x39,  //"C"
	0x5E,  //"D"
	0x79,  //"E"
	0x71,  //"F"
};

/* Other encoding */
//0x77,  //"A"
//0x7C,  //"B"
//0x39,  //"C"
//0x5E,  //"D"
//0x79,  //"E"
//0x71,  //"F"
//0x3D,	 //"G"
//0x76,  //"H"
//0x06,  //"i"
//0x78,  //"K"
//0x38,  //"L"
//0x37,  //"n"
//0x5C,  //"o"
//0x73,  //"P"
//0x50,	 //"r"
//0x78,  //"t"
//0x3E,  //"u"
//0x6E,  //"Y"
//0x40,  //"-"

const uint8_t pa_powerON[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x00, 0x7C, 0x5C, 0x5C, 0x78, 0x5C, 0x5C, 0x00, 0x00, 0x00	 	// " BooKoo "
};

const uint8_t pa_caliK_waitpull[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x00, 0x39, 0x40, 0x5B, 0x79, 0x50, 0x5C, 0x00, 0x00, 0x00		// " C-ZEro "
};

const uint8_t pa_caliK_pull[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x00, 0x00, 0x73, 0x3E, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00		// "  PuLL  "
};


const uint8_t pa_caliK_success[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x00, 0x00, 0x5E, 0x5C, 0x37, 0x79, 0x00, 0x00, 0x00, 0x00		// "  Done  "
};

const uint8_t pa_auto_enter[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x00, 0x00, 0x77, 0x3E, 0x78, 0x5C, 0x00, 0x00, 0x00, 0x00		// "  Auto  "
};

const uint8_t pa_auto_ready[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x00, 0x57, 0x79, 0x77, 0x5E, 0x6E, 0x00, 0x00, 0x00, 0x00		// " Ready  "
};

const uint8_t pa_lowpower_protect[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x00, 0x38, 0x5C, 0x40, 0x7C, 0x77, 0x78, 0x00, 0x00, 0x00,		// " Lo-BAt "
};

const uint8_t pa_incharging[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x39, 0x76, 0x77, 0x50, 0x3D, 0x06, 0x37, 0x3D, 0x00, 0x00		// "charging"
};

const uint8_t pa_infullcharging[DIGIT_TOTAL_NUMS] = {
	0x00, 0x00, 0x39, 0x76, 0x3D, 0x00, 0x5E, 0x5C, 0x37, 0x79, 0x00, 0x00		// "chg done"
};

const uint8_t *p_pattern[DISP_PATTERN_NUMS] = {
	pa_powerON, pa_caliK_waitpull, pa_caliK_pull, pa_caliK_success, pa_auto_enter, pa_auto_ready, pa_lowpower_protect, 
	pa_incharging, pa_infullcharging
};

static uint32_t ani_idx = 0;

const uint8_t pani_powerON[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x7C, 0x5C, 0x5C, 0x78, 0x5C, 0x5C, 0x00, 0x00, 	// BooKoo 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t pani_powerOFF[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x6D, 0x38, 0x79, 0x79, 0x73, 0x06, 0x37, 0x3D, 0x00, 	// SLEEPING
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t pani_caliK_waitpull[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* Private function declarations ---------------------------------------------*/
/* Function prototypes -------------------------------------------------------*/
/**@brief Function Brief.
 *
 * @param[in]   xxx   Parameter description
 * @param[out]  xxx   Parameter description
 */
static void allLED_off(void) {
	/* Erase the digits buffer of TA6932 */
	memset(buf, 0, DIGIT_TOTAL_NUMS * sizeof(uint8_t));
	if(dev_tm1640b_senddata(0, buf, DIGIT_TOTAL_NUMS) != NS_SUCCESS) {
		APP_ERROR_HANDLER(BOOKOO_ERROR_DISP_UPDATE_FAIL);
	}
	if(dev_tm1640b_onoff_ctrl(false) != NS_SUCCESS) {
		APP_ERROR_HANDLER(BOOKOO_ERROR_DISP_UPDATE_FAIL);
	}
}


static void gen_decimal_ctrl(float _val_abs, uint8_t _digits_num, uint8_t * _buf) {
	float decimal_ignore_val = powf(10.0f, _digits_num - 1);
	bool ignore_flag = false;
	int zero_num=0;
	/* 
		Ignore the decimal (when the number is higher than 10^(d-1))
		For example, when have a digits with four 'bits' and desire to reserve one decimal place,
		the number higher than 1000.0 will be hide it's decimal place
		*/
	if(oz_mode) {
		_val_abs *= 0.035274 ;
	}
	if(_val_abs >= decimal_ignore_val) {
		ignore_flag = true;
	}
	/* Otherwise, display the decimal */
	else{
		ignore_flag = false;
	}
	
	/* Decode the number into digits' buffer */
	uint32_t temp = 0.0f;
	if(ignore_flag) {
		temp = (uint32_t)floor(_val_abs);
	}
	else {
		if(oz_mode) {
            temp = (uint32_t)floor(_val_abs * 100.0f);
        }
        else {
            temp = (uint32_t)floor(_val_abs * 10.0f);
        }
	}
	for(uint8_t i = 0; i < _digits_num; i ++) {
		uint32_t temp_bit = powf(10.0f, _digits_num - 1 - i);
		_buf[i] = digits_encoding_tab[temp / temp_bit] | DIGIT_POINT_OFF;
		temp = temp % temp_bit;
	}
	
	/* When ignore the decimal */
	if(false == ignore_flag) {
		if(oz_mode) {
            _buf[_digits_num - 3] |= DIGIT_POINT_ON;
            zero_num = 2;
        } //灏忔暟鐐规樉绀?
		else {
            _buf[_digits_num - 2] |= DIGIT_POINT_ON;
            zero_num = 2;
        }
		
		/* Hide the zeros at the high order, that means it will display '0.0', not '000.0' */
		for(uint8_t i = 0; i < (_digits_num - zero_num); i++) {
			if(_buf[i] == digits_encoding_tab[0]) {
				_buf[i] = 0x00;
			}
			else {
				break;
			}
		}
	}
}


static void mid_gen_decimal_ctrl(float _val_abs, uint8_t _digits_num, uint8_t * _buf) {
	float decimal_ignore_val = powf(10.0f, _digits_num - 1);
	bool ignore_flag = false;
	int zero_num=0;
	/* 
		Ignore the decimal (when the number is higher than 10^(d-1))
		For example, when have a digits with four 'bits' and desire to reserve one decimal place,
		the number higher than 1000.0 will be hide it's decimal place
		*/
	if(oz_mode) {
		_val_abs *= 0.035274 ;
	}

	if(_val_abs >= decimal_ignore_val) {
		ignore_flag = true;
	}
	/* Otherwise, display the decimal */
	else {
		ignore_flag = false;
	}
	/* Decode the number into digits' buffer */
	uint32_t temp = 0.0f;
	if(ignore_flag) {
		temp = (uint32_t)floor(_val_abs);
	}
	else {
        if(oz_mode) {
            temp = (uint32_t)floor(_val_abs * 100.0f);
        }
        else {
            temp = (uint32_t)floor(_val_abs * 10.0f);
        }
	}
    for(uint8_t i = 0; i < _digits_num; i ++) {
        uint32_t temp_bit = powf(10.0f, _digits_num - i - 1);
        _buf[i] = digits_encoding_tab[temp / temp_bit];
        temp = temp % temp_bit;
    }
	/* When ignore the decimal */

	if(false == ignore_flag) {
		if(oz_mode) {
            _buf[_digits_num - 3] |= DIGIT_POINT_ON;
            zero_num = 2;
        } //灏忔暟鐐规樉绀?
		else {
            _buf[_digits_num - 2] |= DIGIT_POINT_ON;
            zero_num = 2;
        }
		
		/* Hide the zeros at the high order, that means it will display '0.0', not '000.0' */
		for(uint8_t i = 0; i < (_digits_num - zero_num); i++) {
			if(_buf[i] == digits_encoding_tab[0]) {
				_buf[i] = 0x00;
			}
			else {
				break;
			}
		}
	}
}


static void disp_timing(uint32_t _timing_ms,float _flowrate, float _mass) {
	memset(buf, 0, DIGIT_NUMERIC_COUNT * sizeof(uint8_t));
	/* Timing digits prepare */
	if(_timing_ms > DISPLAY_TIMING_UP_MS) {
		_timing_ms = DISPLAY_TIMING_UP_MS;
	}
	
	uint8_t tim_min = (_timing_ms / 1000) / 60;
	uint8_t tim_s = (_timing_ms / 1000) % 60;
	ASSERT(tim_min < 10);
	ASSERT(tim_s < 60);
	buf[DIGIT_TIMING_START_IDX + 0] = digits_encoding_tab[tim_min];
	buf[DIGIT_TIMING_START_IDX + 1] = digits_encoding_tab[tim_s / 10];
	buf[DIGIT_TIMING_START_IDX + 2] = digits_encoding_tab[tim_s % 10];

	/* Flowrate digits buffer prepare */
	if(_flowrate >= DISPLAY_FLOWRATE_UP) {
		/* Out of range, display "--" */
		buf[DIGIT_FLOWRATE_START_IDX] = 0x40;
		buf[DIGIT_FLOWRATE_START_IDX + 1] = 0x40;
		buf[DIGIT_FLOWRATE_START_IDX + 2] = 0x40;
	}
	else if(_flowrate >= 0.0f) {
		/* In the range (nonnegative number) */
		uint16_t flowrate_int = (uint16_t)(floor(_flowrate * 10.0f));
		if(_flowrate >= 10.0f) {
            buf[DIGIT_FLOWRATE_START_IDX] = digits_encoding_tab[flowrate_int / 100] | DIGIT_POINT_OFF;
        }
		buf[DIGIT_FLOWRATE_START_IDX + 1] = digits_encoding_tab[(flowrate_int / 10) % 10] | DIGIT_POINT_ON;
		buf[DIGIT_FLOWRATE_START_IDX + 2] = digits_encoding_tab[flowrate_int % 10] | DIGIT_POINT_OFF;
	}
	else {
		/* Negative number, display 0.0 */
		buf[DIGIT_FLOWRATE_START_IDX + 1] = digits_encoding_tab[0] | DIGIT_POINT_ON;;
		buf[DIGIT_FLOWRATE_START_IDX + 2] = digits_encoding_tab[0] | DIGIT_POINT_OFF;
	}
	
	/* Mass digits buffer prepare */
	if((_mass > DISPLAY_MASS_UP_G) || (_mass < DISPLAY_MASS_DOWN_G) || (disp_peeling_indication)) {
		/* Out of range, display "--" */
		memset(buf + DIGIT_MASS_START_IDX, 0x40, 5 * sizeof(uint8_t));
	}
	else if(_mass >= 0.0f) {
		/* Nonegative number */
		gen_decimal_ctrl(fabs(_mass), 5, buf+DIGIT_MASS_START_IDX);
	}
	else if(_mass > -100.0f) {
		/* Negative number */
		buf[DIGIT_MASS_START_IDX + 1] = 0x40;
		gen_decimal_ctrl(fabs(_mass), 3, buf+DIGIT_MASS_START_IDX + 2);
	}
	else {
		/* Negative number */
		buf[DIGIT_MASS_START_IDX + 0] = 0x40;
		gen_decimal_ctrl(fabs(_mass), 4, buf+DIGIT_MASS_START_IDX + 1);
	}
}


static void disp_weight(float _mass) {
	memset(buf, 0, DIGIT_NUMERIC_COUNT * sizeof(uint8_t));
	/* Mass digits buffer prepare */
	if((_mass > DISPLAY_MASS_UP_G) || (_mass < DISPLAY_MASS_DOWN_G) || (disp_peeling_indication)) {
		/* Out of range, display "--" */
		memset(buf + DIGIT_MID_MASS_START_IDX + 1, 0x40, 5 * sizeof(uint8_t));
	}
	else if(_mass >= 0.0f) {
		/* Nonegative number */
		mid_gen_decimal_ctrl(fabs(_mass), 6, buf + DIGIT_MID_MASS_START_IDX);
	}
	else {
		/* Negative number */
		buf[DIGIT_MID_MASS_START_IDX + 0] = 0x40;
		mid_gen_decimal_ctrl(fabs(_mass), 5, buf + DIGIT_MID_MASS_START_IDX + 1);
	}
}


static void disp_powerpercentage(void) {
	uint8_t power = (uint8_t)s_power;

	if(power > 100U) {
		power = 100U;
	}

	memset(buf, 0, sizeof(buf));

	buf[2] = 0x39;
	buf[3] = 0x09;
	buf[4] = 0x0F;
	buf[5] = 0x30;

	if(power >= 100U) {
		buf[6] = digits_encoding_tab[1];
		buf[7] = digits_encoding_tab[0];
		buf[8] = digits_encoding_tab[0];
	}
	else {
		buf[7] = digits_encoding_tab[power / 10U];
		buf[8] = digits_encoding_tab[power % 10U];
	}
}


static void disp_pattern(uint8_t _pa_idx) {
	/* Reserve indication LEDs */
	ASSERT(_pa_idx < DISP_PATTERN_NUMS);
	memcpy(buf, p_pattern[_pa_idx], DIGIT_NUMERIC_COUNT);
}


static void disp_CHGLEDs(uint8_t _flags) {
	uint8_t indicators = (uint8_t)((_flags & 0x07U) << 1U);
	buf[INDICATOR_H_GRID_IDX] &= ~(INDICATOR_H2_BUSY_1 | INDICATOR_H3_BUSY_2 | INDICATOR_H4_BUSY_3);
	buf[INDICATOR_H_GRID_IDX] |= indicators;
}


static void disp_stateLEDs(uint8_t _flags) {
	uint8_t indicators = 0;
	if((_flags & 0x01U) != 0U) {
		indicators |= INDICATOR_H2_BUSY_1;
	}
	if((_flags & 0x02U) != 0U) {
		indicators |= INDICATOR_H3_BUSY_2 | INDICATOR_H5_MINUTE | INDICATOR_H7_SECOND;
	}
	if((_flags & 0x08U) != 0U) {
		indicators |= INDICATOR_H2_BUSY_1 | INDICATOR_H3_BUSY_2 | INDICATOR_H4_BUSY_3 |
			INDICATOR_H6_AUTO;
	}
	if((_flags & 0x20U) != 0U) {
		indicators |= INDICATOR_H8_FLOW;
	}

	buf[INDICATOR_H_GRID_IDX] &= ~(INDICATOR_H2_BUSY_1 | INDICATOR_H3_BUSY_2 |
		INDICATOR_H4_BUSY_3 | INDICATOR_H5_MINUTE | INDICATOR_H6_AUTO |
		INDICATOR_H7_SECOND | INDICATOR_H8_FLOW);
	buf[INDICATOR_H_GRID_IDX] |= indicators;
}


static void disp_busyLEDs(uint8_t _flags) {
	disp_CHGLEDs(_flags);
}


static void update_rightupLEDs(void) {
	buf[INDICATOR_J_GRID_IDX] &= ~(INDICATOR_J1_MUTE | INDICATOR_J2_OZ | INDICATOR_J3_G);
    /* Display Unit */
	if(oz_mode) {
		buf[INDICATOR_J_GRID_IDX] |= INDICATOR_J2_OZ;
    }
	else {
		buf[INDICATOR_J_GRID_IDX] |= INDICATOR_J3_G;
    }
    /* Display sound enable */
	if(!_voice_enable) {
		buf[INDICATOR_J_GRID_IDX] |= INDICATOR_J1_MUTE;
    }
	return;
}


static void force_disp_rightupLEDs(uint8_t _data) {
	uint8_t mask = INDICATOR_J1_MUTE | INDICATOR_J2_OZ | INDICATOR_J3_G;
	buf[INDICATOR_J_GRID_IDX] &= ~mask;
	buf[INDICATOR_J_GRID_IDX] |= _data & mask;
}


static bool ani_play(uint8_t const * _pani, uint32_t const _size) {
	static uint32_t ani_freq_cnt = 0;
	if(ani_freq_cnt >= ((ani_cycle_ms / freq_ms) - 1)) {
		ani_freq_cnt = 0;
		
		if(ani_idx < (_size - (DIGIT_TOTAL_NUMS - 1))) {
			memcpy(buf, _pani+ani_idx, DIGIT_NUMERIC_COUNT);
			ani_idx++;
			return false;
		}
		else{
			ani_idx = 0;
			return true;
		}
	}
	else{
		ani_freq_cnt++;
		return false;
	}
}


static void whole_reset(void) {
	/* Reset animation count */
	ani_cnt = 0;
	ani_state = 0;
	
	/* Reset busy indication */
	disp_busy_indication = false;
	
	/* Reset the buffer and close the LEDs */
	allLED_off();
	
	/* TA6932 power off */
	dev_tm1640b_sleeping();

	/* Charging */
	in_fullcharging = false;
	charging_redLEDs_spd_cnt = 0;

	disp_peeling_indication = false;
	disp_halt_batlevel = false;
}


void digits_disp_init(uint32_t const _freq_ms, uint16_t _ani_cycle_ms) {
	ASSERT(_freq_ms != 0);
    ASSERT(_ani_cycle_ms != 0);
	freq_ms = _freq_ms;
    ani_cycle_ms = _ani_cycle_ms;
	
	dev_tm1640b_init();
	whole_reset();
}


void digits_disp_update_vars(float _mass, float _flowrate, uint32_t _timing) {
	s_mass = _mass;
	s_flowrate = _flowrate;
	s_timing = _timing;
}


void digits_disp_update_power(uint8_t _power) {
	s_power = _power;
}


void digits_disp_update_autovars(float _mass, float _flowrate, uint32_t _timing) {
	s_auto_mass = _mass;
	s_auto_flowrate = _flowrate;
	s_auto_timing = _timing;
}


#define ANI_STARTUP_BLENAME_MS		1000
#define ANI_STARTUP_PWRREMAIN_MS	200
#define ANI_HALT_PWRREMAIN_MS       800
#define ANI_CALIK_WELCOME_MS        800
#define ANI_CALIK_WAITPULL_MS       2000
#define ANI_LOWPWR_PROTECT_MS       1000
#define ANI_INCHARGING_MS           1500
#define ANI_INCHARGING_LEDFLOW_MS	200
#define ANI_EXITCHARGING_DISP_MS	2000
#define ANI_BUSY_LEDFLOW_MS         100
#define ANI_AUTOWELCOME_DISP_MS		1000
#define ANI_AUTOEND_FLASHING_MS		800

void digits_disp_processing(void) {
	static uint32_t disp_busy_freq_cnt = 0;
	bool auto_end_flash_on = false;
	
	switch(display_state) {
		case DISP_HALT: return;
		case DISP_STARTUP: {
//			switch(ani_state) {
//				case 0:
//					if(ani_play(pani_powerON, sizeof(pani_powerON))) {
//						ani_cnt = 0;
//						ani_state = 1;
//					}
                    ani_cnt++;
                    disp_pattern(PA_POWERON);
                    if(ani_cnt > (ANI_STARTUP_PWRREMAIN_MS / freq_ms)) {
						ani_cnt = 0;
//						ani_state = 1;
                        //FIXED:remove show BLEnum
//                        ani_state = 0;
                        display_state = DISP_FINISHSTARTUP;
					}
//				break;
//				case 1:
//					ani_cnt++;
//					memcpy(buf, pa_BLEnum_str, (DIGIT_TOTAL_NUMS - 2) * sizeof(uint8_t));
//					if(ani_cnt > (ANI_STARTUP_BLENAME_MS / freq_ms)) {
//						ani_cnt = 0;
//						ani_state = 0;
//						display_state = DISP_FINISHSTARTUP;
//					}
//				break;
//			}; 
		} break;
			
		case DISP_FINISHSTARTUP: break;

		case DISP_RECOVERABLE_ERROR: {
			uint16_t error_code = recoverable_error_code;
			memset(buf, 0, sizeof(buf));
			buf[0] = 0x79;
			buf[1] = 0x50;
			buf[2] = 0x50;
			for(uint8_t index = 0U; index < 4U; index++) {
				buf[7U - index] = digits_encoding_tab[error_code & 0x0FU];
				error_code >>= 4U;
			}
		} break;
		
		case DISP_WEIGHTING: {
			disp_weight(s_mass);
        } break;
		
        case DISP_TIMING: {
			disp_timing(s_timing, s_flowrate, s_mass);
		} break;
        
		case DISP_CALIK_WAITING: {
			switch(ani_state) {
				case 0: {
					if(ani_play(pani_caliK_waitpull, sizeof(pani_caliK_waitpull))) {
						ani_cnt = 0;
						ani_state = 1;
					}
					else {
						ani_cnt++;
						if(ani_cnt >= (ANI_CALIK_WELCOME_MS/freq_ms)){
							for(uint8_t i = 0; i < DIGIT_NUMERIC_COUNT; i++) {
								buf[i] |= pa_caliK_waitpull[i];
							}
						}
					}
				} break;
				
				case 1: {
					disp_pattern(PA_CALIK_WAITPULL);
					
					ani_cnt++;
					if(ani_cnt >= (ANI_CALIK_WAITPULL_MS/freq_ms)) {
						ani_cnt = 0;
						ani_state = 0;
						display_state = DISP_CALIK_WAITING_ANIDONE;
					}
				} break;
			};
		} break;
				
		case DISP_CALIK_WAITING_ANIDONE: {
            disp_pattern(PA_CALIK_WAITPULL); 
        } break;
        
		case DISP_CALIK_PULL: {
            disp_pattern(PA_CALIK_PULL); 
        } break;
		
        case DISP_CALIK_SUCCESS: {
            disp_pattern(PA_CALIK_SUCCESS); 
        } break;
			
		case DISP_AUTOREADY: {
			/* Display 'Auto' */
			if(false == disp_auto_welcome) {
				ani_state = 1;
			}
			switch(ani_state) {
				case 0:
					ani_cnt++;
					disp_pattern(PA_AUTO_ENTER);
					if(ani_cnt > (ANI_AUTOWELCOME_DISP_MS / freq_ms)) {
						ani_cnt = 0;
						ani_state = 1;
					}
				break;

				case 1:
					disp_timing(s_timing, s_flowrate, s_mass);
				break;

				default:
				break;
			}
		} break;
		
		case DISP_AUTOGOING: {
            disp_timing(s_timing, s_flowrate, s_mass); 
        } break;
		
		case DISP_AUTOEND: {
			uint32_t cur_action = (ani_cnt * freq_ms) / ANI_AUTOEND_FLASHING_MS;
			ani_cnt++;
			
			/* Reset the Flashing */
			if(cur_action >= 12){
				cur_action = 0;
				ani_cnt = 0;
			}
			/* Flashing, LED ON */
			if((cur_action % 2) == 1){
				auto_end_flash_on = true;
                disp_timing(s_auto_timing, s_auto_flowrate, s_auto_mass);
			}
			/* Flashing, LED OFF */
			else{
				memset(buf, 0, DIGIT_NUMERIC_COUNT * sizeof(uint8_t));
				/* Timing division symbol off */
//				buf[8] &= (~0x30);
			}
			
		} break;
				
		case DISP_LOWPOWER_PROTECT: {
			disp_pattern(PA_LOWPOWER_PROTECT);
			ani_cnt++;
			if(ani_cnt > (ANI_LOWPWR_PROTECT_MS / freq_ms)) {
				ani_cnt = 0;
				display_state = DISP_LOWPOWER_PROTECT_WAITING;
			}
		} break;
		
		case DISP_LOWPOWER_PROTECT_WAITING: break;
		
		case DISP_BYEBYE: {
			if(disp_halt_batlevel) {
				ani_cnt++;
				disp_powerpercentage();
				
				if(ani_cnt > (ANI_HALT_PWRREMAIN_MS / freq_ms)) {
					ani_cnt = 0;
					ani_state = 0;
					disp_halt_batlevel = false;
				}
			}
			else{
				whole_reset();
				disp_busy_freq_cnt = 0;
				display_state = DISP_HALT;
				return;
			}
		} break;
		
		case DISP_INCHARGING: {
			switch(ani_state) {
				case 0: {
					/* Display charging */
					disp_pattern(PA_INCHARGING);
					ani_cnt++;
					if(ani_cnt > (ANI_INCHARGING_MS / freq_ms)) {
						ani_cnt = 0x01;
						ani_state = 1;
						memset(buf, 0, DIGIT_NUMERIC_COUNT * sizeof(uint8_t));
					}
				} break;
				
				case 1: {
					/* If charging finish */
					if(in_fullcharging) {
						disp_CHGLEDs(0);
						disp_pattern(PA_INFULLCHARGING);
					}
					/* Otherwise, RED-LEDs */
					else {
						if(charging_redLEDs_spd_cnt > (ANI_INCHARGING_LEDFLOW_MS / freq_ms)) {
							charging_redLEDs_spd_cnt = 0;
							ani_cnt <<= 1;
							if(ani_cnt > 0x05) {
								ani_cnt = 0x01;
							}
                            disp_pattern(PA_INCHARGING);
							// disp_CHGLEDs(ani_cnt);
                            disp_busyLEDs(ani_cnt);
						}
						else {
							charging_redLEDs_spd_cnt ++;
						}
					}
				} break;
			};
		} break;
		
        case DISP_TESTLED: {
            memset(buf, 0xff, (DIGIT_TOTAL_NUMS) * sizeof(uint8_t));
		} break;
        
		case DISP_EXITCHARGING:
				/* Display the battery remain */
				disp_CHGLEDs(0);
				disp_powerpercentage();
				ani_cnt++;
				if(ani_cnt > (ANI_EXITCHARGING_DISP_MS/freq_ms)){
					ani_cnt = 0;
					ani_idx = 0;
					display_state = DISP_BYEBYE;
					disp_halt_batlevel = false;
				}
		break;
                
		default: break;
	};
	
	/* 3-LEDs busy indications */
	if(disp_busy_indication) {
		force_disp_rightupLEDs(0x00);
		if(disp_busy_freq_cnt > ((ANI_BUSY_LEDFLOW_MS / freq_ms) - 1)) {
			disp_busy_freq_cnt = 0;
			disp_busy_flags <<= 1;
		}
		else{
			disp_busy_freq_cnt ++;
		}
		
		if(disp_busy_flags > 0x04) {
			disp_busy_flags = 0x01;
		}
		disp_busyLEDs(disp_busy_flags);
	}
	else {
		if(display_state == DISP_WEIGHTING) {
			disp_stateLEDs(0x01);
            update_rightupLEDs();
		}
		else if(display_state == DISP_TIMING) {
			disp_stateLEDs(0x23);
            update_rightupLEDs();
		}
		else if(display_state == DISP_AUTOREADY) {
			disp_stateLEDs(0x28);
			update_rightupLEDs();
		}
		else if(display_state == DISP_AUTOGOING) {
			disp_stateLEDs(0x2A);
			update_rightupLEDs();
		}
		else if(display_state == DISP_AUTOEND) {
			disp_stateLEDs(auto_end_flash_on ? 0x2A : 0x28);
            update_rightupLEDs();
		}
        else if(display_state == DISP_TESTLED) {
            force_disp_rightupLEDs(0xFF);
        }
        else if(display_state == DISP_INCHARGING) {
        }
		else {
			disp_stateLEDs(0x00);
            force_disp_rightupLEDs(0x00);
		}
	}
	/* Send the digits buffer to the chip */
	if(dev_tm1640b_senddata(0, buf, DIGIT_TOTAL_NUMS) != NS_SUCCESS){
		APP_ERROR_HANDLER(BOOKOO_ERROR_DISP_UPDATE_FAIL);
	}
	if(dev_tm1640b_onoff_ctrl(true) != NS_SUCCESS){
		APP_ERROR_HANDLER(BOOKOO_ERROR_DISP_UPDATE_FAIL);
	}
}


void digits_disp_autowelcome(bool _enable) {
	disp_auto_welcome = _enable;
}


void digits_disp_req(digits_disp_req_e const _req) {
	if(_req == DISP_HALT_REQ) {
		if(display_state != DISP_HALT) {
			ani_idx = 0;
			ani_cnt = 0;
			display_state = DISP_BYEBYE;
			disp_halt_batlevel = true;
		}
	}
	else if(_req == DISP_STARTUP_REQ) {
		if(display_state == DISP_HALT) {
			/* Wakeup TA6932 */
			if(dev_tm1640b_wakeup() != NS_SUCCESS) {
				APP_ERROR_HANDLER(BOOKOO_ERROR_DISP_WAKEUP_FAIL);
			}
		}
		if((display_state == DISP_HALT) ||
		   (display_state == DISP_FINISHSTARTUP) ||
		   (display_state == DISP_RECOVERABLE_ERROR)) {
			ani_cnt = 0U;
			display_state = DISP_STARTUP;
		}
	}
	else if(_req >= DISP_WEIGHTING_REQ && _req <= DISP_CALIK_SUCCESS_REQ) {
		if(display_state != (_req - DISP_WEIGHTING_REQ + DISP_WEIGHTING)) {
			ani_cnt = 0;
			ani_state = 0;
			display_state = (_req - DISP_WEIGHTING_REQ + DISP_WEIGHTING);
		}
	}
	else {
		switch(_req) {
			case DISP_LEFTPRESS_REQ: buf[INDICATOR_H_GRID_IDX] |= INDICATOR_H1_LEFT_KEY; break;
			case DISP_LEFTRELEASE_REQ: buf[INDICATOR_H_GRID_IDX] &= ~INDICATOR_H1_LEFT_KEY; break;
			case DISP_RIGHTPRESS_REQ: buf[INDICATOR_J_GRID_IDX] |= INDICATOR_J6_RIGHT_KEY; break;
			case DISP_RIGHTRELEASE_REQ: buf[INDICATOR_J_GRID_IDX] &= ~INDICATOR_J6_RIGHT_KEY; break;
			case DISP_LOWPOWER_INTOWARNING_REQ: buf[14] |= 0x04; break;
			case DISP_LOWPOWER_EXITWARNING_REQ: buf[14] &= (~0x04); break;
			case DISP_LOWPOWER_PROTECT_REQ: 
				ani_cnt = 0;
				ani_state = 0;
				display_state = DISP_LOWPOWER_PROTECT;
			break;
			
			case DISP_INTOBUSY_REQ: disp_busy_indication = true; break;
			case DISP_EXITBUSY_REQ: disp_busy_indication = false; break;
			/* Upper right corner indicator light */
			case DISP_VOICE_ENABLE_REQ:	_voice_enable = true;break;	
			case DISP_VOICE_DISABLE_REQ:_voice_enable = false;break;	
			case DISP_OZ_MODE_REQ: 			oz_mode = true;break;
			case DISP_G_MODE_REQ: 			oz_mode = false;break;
		
			
			/* Clean 'INTO WARNING' indication */
			case DISP_INCHARGING_REQ: 
				if(dev_tm1640b_wakeup() != NS_SUCCESS) {
					APP_ERROR_HANDLER(BOOKOO_ERROR_DISP_WAKEUP_FAIL);
				}
				ani_cnt = 0;
				ani_state = 0;
				/* Since the charging may happens anywhere and all the system will be set to SLEEPING, the indication LEDs should be cleaned */
				buf[INDICATOR_H_GRID_IDX] = 0x00;
				buf[INDICATOR_J_GRID_IDX] = 0x00;
				buf[13] = 0x00;
				buf[14] = 0x00;
				charging_redLEDs_spd_cnt = 0;
				disp_busy_indication = false;
				in_fullcharging = false;
				display_state = DISP_INCHARGING;
			break;
			
            case DISP_TESTLED_REQ: {
				if(dev_tm1640b_wakeup() != NS_SUCCESS){
					APP_ERROR_HANDLER(BOOKOO_ERROR_DISP_WAKEUP_FAIL);
				}
				ani_cnt = 0;
				ani_state = 0;
				charging_redLEDs_spd_cnt = 0;
				disp_busy_indication = false;
				in_fullcharging = false;
				display_state = DISP_TESTLED;
			} break;
                
			case DISP_INFULLCHARGING_REQ:
				in_fullcharging = true;
			break;
			
			case DISP_EXITCHARGING_REQ:
				if(display_state == DISP_INCHARGING) {
					ani_cnt = 0;
					ani_state = 0;
					display_state = DISP_EXITCHARGING;
				}
			break;
				
			case DISP_PEELING_REQ:
				s_mass = 0.0f;
				s_flowrate = 0.0f;
				disp_peeling_indication = false;
			break;
			case DISP_EXITPEELING_REQ: disp_peeling_indication = false; break;
			default: break;
		};
	}
}


digits_disp_state_e digits_disp_stateget(void) {
 	return display_state;
}

void digits_disp_recoverable_error(uint16_t error_code)
{
	if(display_state == DISP_HALT) {
		if(dev_tm1640b_wakeup() != NS_SUCCESS) {
			APP_ERROR_HANDLER(BOOKOO_ERROR_DISP_WAKEUP_FAIL);
		}
	}
	recoverable_error_code = error_code;
	disp_busy_indication = false;
	display_state = DISP_RECOVERABLE_ERROR;
}


void digits_disp_errorcode(uint16_t _err_code) {
	/* Disable the hardware SPIM of the tm1640b */
	dev_tm1640b_sleeping();
	
	/* Disable IRQ */
	__disable_irq();
	
	/* Enable software SPIM of the tm1640b */
	dev_tm1640b_init_software();
	dev_tm1640b_wakeup_software();
	
	/* Clean all the buffer */
	memset(buf, 0x00, sizeof(uint8_t) * DIGIT_TOTAL_NUMS);
	
	/* Type: Err xxxx */
	buf[0] = 0x79;
	buf[1] = 0x50;
	buf[2] = 0x50;
	
	uint16_t err_code = _err_code;
	for(uint8_t i = 0; i < 4;i++){
		uint8_t byte = (uint8_t)(err_code & 0x0F);
		err_code >>= 4;

		if(byte <= 0x0F){
			buf[7-i] = digits_encoding_tab[byte];
		}
	}
	
	/* Send the data to the tm1640b */
	dev_tm1640b_senddata_software(0, buf, DIGIT_TOTAL_NUMS);
	dev_tm1640b_onoff_ctrl_software(true);
}


void digits_disp_enterDFU_software(void) {
	const uint8_t pa_enterDFU[DIGIT_TOTAL_NUMS] = {
		0x00, 0x06, 0x37, 0x00, 0x5E, 0x71, 0x3E, 0x00, 0x00, 0x00		// " in DFU "
	};
		
	/* Disable the hardware SPIM of the tm1640b */
	dev_tm1640b_sleeping();
	
	/* Enable software SPIM of the tm1640b */
	dev_tm1640b_init_software();
	dev_tm1640b_wakeup_software();
	
	/* Clean all the buffer */
	memset(buf, 0x00, sizeof(uint8_t) * DIGIT_TOTAL_NUMS);
	memcpy(buf, pa_enterDFU, sizeof(uint8_t) * (DIGIT_TOTAL_NUMS-2));
	dev_tm1640b_senddata_software(0, buf, DIGIT_TOTAL_NUMS);
	dev_tm1640b_onoff_ctrl_software(true);
}

/**
 * @}
 */

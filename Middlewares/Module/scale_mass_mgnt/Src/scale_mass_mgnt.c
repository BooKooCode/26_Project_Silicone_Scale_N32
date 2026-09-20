/** @file
 *
 * @defgroup filename.c
 * @{
 * @ingroup  
 * @brief    file description
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "dev_status.h"
#include "FreeRTOS.h"
#include "mass_meas.h"
#include "rtt_mass_telemetry.h"
#include "scale_mass_mgnt.h"
#include "scale_peeling_timeout.h"
#include "sleep_probe.h"
#include "task.h"
#include <math.h>
#include <string.h>
#include "app_error.h"
#include "dev_cs1237.h"
#include "log.h"

#include "filter.h"
#include "bessel2.h"
#include "sliding_filter.h"

#include "bookoo_error_def.h"

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(EXPR) _Static_assert((EXPR), #EXPR)
#endif

STATIC_ASSERT(SLEEP_PROBE_STABLE_SAMPLE_COUNT > 1U);
STATIC_ASSERT(SLEEP_PROBE_STABLE_SAMPLE_COUNT <= MASS_MGNT_RAWDATA_STAT_WIN);

#if DEV_ENABLED
#define SCALE_MASS_LOG_INFO(...) do { (void)LOG_INFO("scale_mass_mgnt", __VA_ARGS__); } while (0)
#else
#define SCALE_MASS_LOG_INFO(...) do { } while (0)
#endif
/* Defines -------------------------------------------------------------------*/


/* Common structure used in mass calibration */
typedef struct _mass_mgnt_calipack_s {
	float sum;
	float readypull_val;
	uint32_t cnt;
	uint32_t cnt_max;
} mass_mgnt_calipack_s;

/* Common structure used in mass calculation */
typedef struct _mass_mgnt_calc_s{
	/* Mass data in unit 'g', which directly converted from the sensor reading result */
	float mass_g_rawdata;
	/* Offset mass in unit 'g', which represents the inherent mass read by the sensor after startup */
	float mass_g_cali_offset;
	/* The ratio between the sensor reading value and the actual mass */
	float mass_g_cali_k;
	/* Peeling mass in unit 'g', which applied to the quantified mass data */
	float mass_g_peeling;
	/* Manager state */
	uint8_t state;
}mass_mgnt_calc_s;

typedef struct _zerocomp_1DKalman_s{
	/* 1D-Kalman filter */
	float q;
	float r;
	
	float x;
	float g;
	float p;
}zerocomp_1DKalman_s;
//#define ZEROCOMP_1DKALMAN_Q		0.001f
#define ZEROCOMP_1DKALMAN_Q		0.001f
#define ZEROCOMP_1DKALMAN_R		0.1f
/* Private variables ---------------------------------------------------------*/
/* Statistical analysis */
float rawdata_mean = 0.0f;
float rawdata_std = 0.0f;

static float rawdata_stat_buf[MASS_MGNT_RAWDATA_STAT_WIN] = {0};
static uint32_t rawdata_stat_idx = 0;
static uint32_t rawdata_stat_count = 0;

static float filter_rawdata_std = 0;

static bool stable_flag = false;
static bool standby_stable_flag = false;
static bool last_stable_flag = false;

/* Offset calibration object */
static mass_mgnt_calipack_s cali_k;

/* Startup offset calibration object */
static mass_mgnt_calipack_s cali_offset;

/* Mass manager object */
static mass_mgnt_calc_s mass_mgnt;
static scale_peeling_session_t peeling_session;

/* Mass data buffer used in numerical differential */
static float rawdata_diff_buf[MASS_MEAS_RAW_DIFF_BUFF_COUNT] = {0};

/* Pointer to the mass datapack */
static mass_mgnt_datapack_s * mass = NULL;
static mass_mgnt_datapack_s * flowrate = NULL;

/* Sampling interval used in numerical differential */
static float Ts = 0;

/* 
	Since the sensor value will dirft during the WAKEUP process,
	which may influence the offset calculation.
*/
static uint32_t wakeup_jump_cnt = 0;

/* Zero-dirft compensation */
bool comp_reset = false;
uint8_t entercomp_actioncnt = 0;				// count after entering compensation core
float zerocomp = 0.0f;									// compensation value
float mass_estimatesum = 0.0f;
float mass_aftercomp = 0.0f;
static uint8_t flowrate_comp_delay = 0;
static uint8_t flowrate_bandcomp_delay = MASS_MEAS_FLOWRATE_BANDCOMP_COUNT;
static uint8_t estimatesum_cnt = 0;

static float debug_massraw_buf[13] = {0};
static float debug_massaftercomp_buf[4] = {0};

STATIC_ASSERT(MASS_MEAS_HISTORY_DELAYLINE_COUNT > 1);
STATIC_ASSERT(SCALE_PEELING_TIMEOUT_CYCLES > 0U);
static float mass_history_buf[MASS_MEAS_HISTORY_DELAYLINE_COUNT] = {0};

static uint32_t mass_history_wr_idx = 0;
static uint32_t mass_history_filled = 0;
static zerocomp_1DKalman_s zerocomp_1dkalman_param = {
	.q = ZEROCOMP_1DKALMAN_Q,
	.r = ZEROCOMP_1DKALMAN_R,
};

static float un_slide = 0;
static float after_slide = 0;

/* Debug observation of the filtering chain.
 * Both variables filter the raw CS1237 ADC sample g_cs1237_debug_raw DIRECTLY,
 * without the unit conversion to mass(g):
 *   g_mass_bessel_only  : raw through Bessel only
 *   g_mass_bessel_slide : raw through Bessel + sliding window (no std gating) */
float g_mass_bessel_only = 0.0f;
float g_mass_bessel_slide = 0.0f;

/* Quantified result after debounce filtering */
float filt_quatraw = 0.0f;
/* Debounce filtering count */
static uint8_t filt_cnt = 0;

/* Zero compesation kalman filter */
//GEN_KALMAN_FILTER_1D(zerocomp, 1.0f, 0.0f, 1.0f, 0.001f, 0.1f, float);
GEN_KALMAN_FILTER_1D(flowrate, 1.0f, 0.0f, 1.0f, 0.1f, 0.1f, float);

/* Flowrate smoothness */
static bool flowratesmooth_enable = false;

/* Hyperparameters for the compensation algorithm */
typedef struct _compensation_algorithm_pramas {
	float fs;                           /* Full Scales in unit 'g' */
	float calik_fluctuation;            /* Standard deviation threshold used in stability assessment */
	float std_th;
	float fs_mVperV;
	uint32_t calioff_cnt_max;           /* Numbers of data used in mass offset calibration */
	uint32_t calik_cnt_max;             /* Numbers of data used in ratio calibration */
	uint32_t wakeupjump_cnt_max;        /* Numbers of jumping count used in wakeup powerON */
} compensation_algorithm_pramas_t;


const compensation_algorithm_pramas_t hualanhai1kg_1mvpv = {
	.fs = 1000.0f,
	.calik_fluctuation = 0.2f,
	.std_th = 0.04f,
	.fs_mVperV = 1.0f,
	
	.calioff_cnt_max = MASS_MEAS_HUALANHAI1KG_CALIOFF_COUNT,
	.calik_cnt_max = MASS_MEAS_HUALANHAI1KG_CALIK_COUNT,
	.wakeupjump_cnt_max = MASS_MEAS_HUALANHAI1KG_WAKEUP_COUNT
};

const compensation_algorithm_pramas_t hongbo2kg_2mvpv = {
	.fs = 3000.0f,
	.calik_fluctuation = 0.15f,
	.std_th = 0.04f,
	.fs_mVperV = 2.0f,
	
	.calioff_cnt_max = MASS_MEAS_HONGBO2KG_CALIOFF_COUNT,
	.calik_cnt_max = MASS_MEAS_HONGBO2KG_CALIK_COUNT,
	.wakeupjump_cnt_max = MASS_MEAS_HONGBO2KG_WAKEUP_COUNT
};

const compensation_algorithm_pramas_t nuosheng3kg_2mvpv = {
	.fs = 3000.0f,
	.calik_fluctuation = 0.15f,
	.std_th = 0.04f,
	.fs_mVperV = 2.0f,
	
	.calioff_cnt_max = MASS_MEAS_NUOSHENG_CALIOFF_COUNT,
	.calik_cnt_max = MASS_MEAS_NUOSHENG_CALIK_COUNT,
	.wakeupjump_cnt_max = MASS_MEAS_NUOSHENG_WAKEUP_COUNT
};

const compensation_algorithm_pramas_t *sensor_params_tab[MASS_MGNT_PARAMS_TABSIZE] = {
	&hualanhai1kg_1mvpv, &hongbo2kg_2mvpv, &nuosheng3kg_2mvpv
};

const compensation_algorithm_pramas_t *sensor_params = NULL;

const float s_standard_weight_table[MASS_STANDARD_WEIGHT_COUNT] = {
    100.0f, 500.0f, 1000.0f
};


MASS_STANDARD_WEIGHT_TABLE s_cali_standard_use = MASS_STANDARD_100G;

/* bessel2 */
#define BES_CUTOFF_FREQ 2.0f
#define BES_SAMPLE_FREQ 40.0f
#define BES_CUTOFF_FREQ_MIN 0.1f
#define BES_CUTOFF_FREQ_MAX 15.0f
static BesselFilter2nd besselfilter;
static float s_filter_cutoff_hz = BES_CUTOFF_FREQ;

/* slide */
#define SLIDING_BUFF  200
#define SLIDING_WINDOW_DEFAULT 100
static sliding_stat_t slide;
static float sliding_stat_buf[SLIDING_BUFF] = {0};
static uint32_t s_filter_window = SLIDING_WINDOW_DEFAULT;

/* Independent observation-chain filter state, used only to produce
 * g_mass_bessel_only / g_mass_bessel_slide from g_cs1237_debug_raw directly */
static BesselFilter2nd bessel_debug;
static sliding_stat_t slide_debug;
static float sliding_stat_buf_debug[SLIDING_BUFF] = {0};

static uint32_t last_sample_count = 0U;

static uint32_t need_comp = false;



/* Private function declarations ---------------------------------------------*/
/* Function prototypes -------------------------------------------------------*/
/**@brief Function Brief.
 *
 * @param[in]   xxx   Parameter description
 * @param[out]  xxx   Parameter description
 */


static float zerocomp_kalmanfilter(float _in) {
	zerocomp_1dkalman_param.x = zerocomp_1dkalman_param.x;
	zerocomp_1dkalman_param.p += zerocomp_1dkalman_param.q;
	zerocomp_1dkalman_param.g = zerocomp_1dkalman_param.p / (zerocomp_1dkalman_param.p + zerocomp_1dkalman_param.r);
	
	zerocomp_1dkalman_param.x += zerocomp_1dkalman_param.g * (_in - zerocomp_1dkalman_param.x);
	zerocomp_1dkalman_param.p = (1.0f - zerocomp_1dkalman_param.g) * zerocomp_1dkalman_param.p;
	return zerocomp_1dkalman_param.x;
}


static void zerocomp_reset(float _init_zerocomp) {
	/* Filter reset */
	zerocomp = _init_zerocomp;
	zerocomp_1dkalman_param.x = _init_zerocomp;
	zerocomp_1dkalman_param.p = 0.0f;
}


static void cs1237_err_handler(uint32_t _err_code) {
//	APP_ERROR_CHECK(_err_code);
}


static void calculate_SD(float data[], uint32_t data_len, float * _res_mean, float * _res_std) {
    ASSERT(data != NULL);
    ASSERT(data_len > 1);
    ASSERT(_res_mean != NULL);
    ASSERT(_res_std != NULL);

    float mean = 0.0f;
    float M2   = 0.0f;   
    uint32_t i;

    for (i = 0; i < data_len; i++) {
        float x = data[i];
        float delta = x - mean;
        mean += delta / (i + 1);
        float delta2 = x - mean;
        M2 += delta * delta2;
    }

    *_res_mean = mean;
    *_res_std  = sqrtf(M2 / (data_len - 1));
}

static void cali_k_reset(void) {
	cali_k.sum = 0;
	cali_k.cnt = 0;
	cali_k.cnt_max = sensor_params->calik_cnt_max;
}


static void cali_offset_reset(void) {
	cali_offset.sum = 0;
	cali_offset.cnt = 0;
	cali_offset.cnt_max = sensor_params->calioff_cnt_max;
}


static void mass_core_reset(void) {
	/* Zero-dirft compensation reset */
	comp_reset = true;
	entercomp_actioncnt = 0;
//	zerocomp = 0.0f;
	mass_estimatesum = 0.0f;
	mass_aftercomp = 0.0f;
	flowrate_comp_delay = 0;
	flowrate_bandcomp_delay = MASS_MEAS_FLOWRATE_BANDCOMP_COUNT;
	estimatesum_cnt = 0;
	
	memset(debug_massraw_buf, 0x00, sizeof(float)*13);
	memset(debug_massaftercomp_buf, 0x00, sizeof(float)*4);
}


static void peeling_output_update(void) {
	mass->absolute_mass = mass_aftercomp;
	mass->quat_after_peeling = mass_aftercomp - mass_mgnt.mass_g_peeling;
	mass->quat_after_peeling_oz = mass->quat_after_peeling / 28.3495f;
	mass->quat_after_peeling = roundf(mass->quat_after_peeling * 10.0f) / 10.0f;
	mass->quat_after_peeling_oz = roundf(mass->quat_after_peeling_oz * 10.0f) / 10.0f;
}


static void manual_peeling_output_reset(void) {
	for(uint8_t i = 0; i < MASS_MEAS_RAW_DIFF_BUFF_COUNT; i++) {
		rawdata_diff_buf[i] = mass->filtered;
	}
	mass->quat_after_peeling = 0.0f;
	mass->quat_after_peeling_oz = 0.0f;
	flowrate->raw = 0.0f;
	flowrate->filtered = 0.0f;
	flowrate->quantified = 0.0f;
	flowrate->quantified_1 = 0.0f;
	kalman_filter1d_reset_flowrate(0.0f);
}


static void peeling_complete(float tare,
							 const scale_peeling_estimate_t *estimate,
							 bool forced) {
	uint32_t elapsed_cycles = peeling_session.elapsed_cycles;
	(void)elapsed_cycles;

	mass_mgnt.mass_g_peeling = tare;
	peeling_output_update();
	manual_peeling_output_reset();
	scale_peeling_session_complete(&peeling_session);
	mass_mgnt.state = MASS_ACQ;
	entercomp_actioncnt = 0U;
	mass_estimatesum = 0.0f;

	if(forced && (estimate != NULL)) {
		SCALE_MASS_LOG_INFO("Peeling timeout-forced: cycles=%lu mode=%u trend=%f residual=%f tare=%f",
							(unsigned long)elapsed_cycles,
							(unsigned int)estimate->mode,
							estimate->trend_delta,
							estimate->residual,
							tare);
	}
	else {
		SCALE_MASS_LOG_INFO("Peeling stable: cycles=%lu tare=%f",
							(unsigned long)elapsed_cycles,
							tare);
	}
}


static void peeling_cancel(void) {
	uint32_t elapsed_cycles = peeling_session.elapsed_cycles;
	(void)elapsed_cycles;

	mass_mgnt.mass_g_peeling = scale_peeling_session_cancel(&peeling_session);
	mass_mgnt.state = MASS_ACQ;
	SCALE_MASS_LOG_INFO("Peeling cancelled: cycles=%lu", (unsigned long)elapsed_cycles);
}


static void mass_mgnt_wholereset(void) {
	/* Mass manager */
	mass_mgnt.state = HALT;
	mass_mgnt.mass_g_peeling = 0;
	scale_peeling_session_init(&peeling_session);
	
	/* Calibration */
	cali_offset_reset();
	cali_k_reset();
	
	/* Statistical analysis */
	memset(rawdata_stat_buf, 0, sizeof(float)*MASS_MGNT_RAWDATA_STAT_WIN);
	stable_flag = false;
	standby_stable_flag = false;
	last_stable_flag = false;
	rawdata_stat_idx = 0;
	rawdata_stat_count = 0;
	rawdata_mean = 0.0f;
	rawdata_std = 0.0f;
	filter_rawdata_std = 0.0f;
	
	/* Numerical differential */
	memset(rawdata_diff_buf, 0, sizeof(float)*MASS_MEAS_RAW_DIFF_BUFF_COUNT);
	
	/* Peeling compensation count */
	mass_core_reset();
	zerocomp = 0.0f;
	
	/* Debounce filtering */
	filt_quatraw = 0.0f;
	filt_cnt = 0;
	un_slide = 0.0f;
	after_slide = 0.0f;
	g_mass_bessel_only = 0.0f;
	g_mass_bessel_slide = 0.0f;
	bessel2_reset(&besselfilter, 0.0f);
	sliding_stat_reset(&slide);
	bessel2_reset(&bessel_debug, 0.0f);
	sliding_stat_reset(&slide_debug);
	
	/* Jumping count in WAKEUP process */
	wakeup_jump_cnt = 0;
	
	/* Flowrate smoothness */
	flowratesmooth_enable = false;

	/* Mass history */
	memset(mass_history_buf, 0, sizeof(mass_history_buf));
	mass_history_wr_idx = 0;
	mass_history_filled = 0;
}

static void mass_mgnt_acquisition_window_reset(void)
{
	memset(rawdata_stat_buf, 0, sizeof(rawdata_stat_buf));
	stable_flag = false;
	standby_stable_flag = false;
	last_stable_flag = false;
	rawdata_stat_idx = 0U;
	rawdata_stat_count = 0U;
	rawdata_mean = 0.0f;
	rawdata_std = 0.0f;
	filter_rawdata_std = 0.0f;
	filt_quatraw = 0.0f;
	filt_cnt = 0U;
	un_slide = 0.0f;
	after_slide = 0.0f;
	g_mass_bessel_only = 0.0f;
	g_mass_bessel_slide = 0.0f;
	bessel2_reset(&besselfilter, mass_mgnt.mass_g_rawdata);
	sliding_stat_reset(&slide);
	bessel2_reset(&bessel_debug, (float)g_cs1237_debug_raw);
	sliding_stat_reset(&slide_debug);
}


static void getraw_and_calcSD(int32_t cs1237_res, uint32_t stable_sample_count) {
	ASSERT(stable_sample_count > 1U);
	ASSERT(stable_sample_count <= MASS_MGNT_RAWDATA_STAT_WIN);
	last_stable_flag = stable_flag;

	/* Debug observation chain: filter the snapshotted sample directly (no unit conversion).
	 * Bessel, then a fixed sliding-window average (no std gating). */
	const float cs1237_raw_f = (float)cs1237_res;
	g_mass_bessel_only = bessel2_update(&bessel_debug, cs1237_raw_f);
	sliding_stat_push(&slide_debug, g_mass_bessel_only);
	if(sliding_stat_ready(&slide_debug)) {
		sliding_stat_get(&slide_debug);
		g_mass_bessel_slide = slide_debug.mean;
	}
	else {
		g_mass_bessel_slide = slide_debug.sum / slide_debug.count;
	}

	/*
		CS1237,24-bit ADC, scale_s = x*1000 / (2^23 * 128) / fs_mVperV = x / 1073741.824
		the 24th bit of the rawdata is the sign, thus the measurement range is -2^23 to 2^23-1
	*/
	float scale_s = (float)cs1237_res / 1073741.824f / 2.0f / sensor_params->fs_mVperV;
	mass_mgnt.mass_g_rawdata = scale_s * sensor_params->fs;
	const float mass_raw_g = mass_mgnt.mass_g_rawdata;
    
    /* bessel calculation */
    mass_mgnt.mass_g_rawdata = bessel2_update(&besselfilter, mass_mgnt.mass_g_rawdata);
	const float mass_bessel_g = mass_mgnt.mass_g_rawdata;
	/* Statistical calculation */
	rawdata_stat_buf[rawdata_stat_idx] = mass_mgnt.mass_g_rawdata;
	rawdata_stat_idx++;
	if(rawdata_stat_count < MASS_MGNT_RAWDATA_STAT_WIN) {
		rawdata_stat_count++;
	}
	if(rawdata_stat_idx % MASS_MGNT_RAWDATA_STAT_WIN == 0) {
        rawdata_stat_idx = 0;
    }
	if(rawdata_stat_count >= stable_sample_count) {
		calculate_SD(rawdata_stat_buf, rawdata_stat_count, &rawdata_mean, &filter_rawdata_std);
	}
	else {
		rawdata_mean = mass_mgnt.mass_g_rawdata;
		filter_rawdata_std = sensor_params->std_th + 1.0f;
	}

    /***** sliding filter *****/
    un_slide = mass_mgnt.mass_g_rawdata;
	/* Quantification result peeling */
    if(filter_rawdata_std <= 0.05f) {
        sliding_stat_push(&slide,mass_mgnt.mass_g_rawdata);
        if(sliding_stat_ready(&slide)) {
            sliding_stat_get(&slide);
            after_slide = slide.mean;
        }
        else {
            after_slide = slide.sum / slide.count;
        }
    }
    else {
        after_slide = un_slide;
        sliding_stat_reset(&slide);
    }
    
    mass_mgnt.mass_g_rawdata = after_slide;
	const rtt_mass_telemetry_sample_t telemetry_sample = {
		.tick_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
		.adc_raw = cs1237_res,
		.mass_raw_g = mass_raw_g,
		.mass_bessel_g = mass_bessel_g,
		.mass_sliding_g = mass_mgnt.mass_g_rawdata,
	};
	rtt_mass_telemetry_submit(&telemetry_sample);
   
    
	/* Stability assessment */
	if((rawdata_stat_count >= stable_sample_count) && (filter_rawdata_std <= sensor_params->std_th)) {
		stable_flag = true;
	}
	else {
		stable_flag = false;
	}
    standby_stable_flag = ((rawdata_stat_count >= MASS_MGNT_RAWDATA_STAT_WIN) && (filter_rawdata_std <= 0.2f));
}


static void mass_calc_core(void) {
	/************ Raw data calculation ************/
	/* Mass data after calibration */
	mass->raw = (mass_mgnt.mass_g_rawdata - mass_mgnt.mass_g_cali_offset) * mass_mgnt.mass_g_cali_k;
	
	/************ Mass data processing ************/
	mass->filtered = mass->raw;
     STATIC_ASSERT(MASS_MEAS_RAW_DIFF_BUFF_COUNT > 1);
	if(mass->filtered != rawdata_diff_buf[MASS_MEAS_RAW_DIFF_BUFF_COUNT - 1]) {
		float temp_rawbuf[MASS_MEAS_RAW_DIFF_BUFF_COUNT - 1] = {0};
		memcpy(temp_rawbuf, &rawdata_diff_buf[1], sizeof(float) * (MASS_MEAS_RAW_DIFF_BUFF_COUNT - 1));
		rawdata_diff_buf[MASS_MEAS_RAW_DIFF_BUFF_COUNT - 1] = mass->filtered;
		memcpy(rawdata_diff_buf, temp_rawbuf, sizeof(float) * (MASS_MEAS_RAW_DIFF_BUFF_COUNT - 1));
		
		/* Numerical differential */
		float temp_sum = 0;
		for(uint8_t i = 0; i < (MASS_MEAS_RAW_DIFF_BUFF_COUNT - 1); i++) {
			for(uint8_t j = i + 1; j < MASS_MEAS_RAW_DIFF_BUFF_COUNT; j++) {
				temp_sum += ((rawdata_diff_buf[j] - rawdata_diff_buf[i]) / (Ts * (j - i)));
			}
		}
		flowrate->raw = temp_sum / ((MASS_MEAS_RAW_DIFF_BUFF_COUNT*(MASS_MEAS_RAW_DIFF_BUFF_COUNT-1))/2.0);
	}
	/* Numerical differential buffer update */

	/* If compensation need */

    if(stable_flag && (fabs(flowrate->raw) <= 0.1f)) {
        if(flowrate_bandcomp_delay > 0) {
            flowrate_bandcomp_delay--;
        }
        else {
            need_comp = true;
        }
    }
	else {
		need_comp = false;
		flowrate_comp_delay = 0;
		flowrate_bandcomp_delay = MASS_MEAS_FLOWRATE_BANDCOMP_COUNT;
	}
	
	/* Compensation core */
	static uint8_t actionstop_cnt = 0;

    actionstop_cnt = 13;

	if(need_comp) {
		if(entercomp_actioncnt < actionstop_cnt) {
			mass_aftercomp = mass->filtered - zerocomp;			
            mass_estimatesum += mass_aftercomp;

			entercomp_actioncnt++;
		}
		else if(entercomp_actioncnt == actionstop_cnt) {
			/* Mass calculation estimation */
            mass_aftercomp = mass_estimatesum / 13.0f;
            
            /* quantify */
			if(fabs(mass_aftercomp) < 0.1f) {
				mass->quantified = 0.0f;
			}
            else {
				mass->quantified = mass_aftercomp;
			}
            
			/* Zero compensation reset */
			zerocomp_reset(mass_aftercomp + zerocomp - mass_aftercomp);
			zerocomp_kalmanfilter(zerocomp);

			entercomp_actioncnt++;
		}
		else{
            comp_reset = false;
			zerocomp = zerocomp_kalmanfilter(mass->filtered - mass->quantified);
            mass_aftercomp = mass->quantified;
		}
	}
	else{
		entercomp_actioncnt = 0;
		mass_estimatesum = 0.0f;
		mass_aftercomp = mass->filtered - zerocomp;
	}
	/* Finally */
	last_stable_flag = stable_flag;
}


static void mass_acq(void) {
	/************ Mass data calculation ************/
	mass_calc_core();

	mass->absolute_mass = mass_aftercomp;
    mass->quat_after_peeling = mass_aftercomp - mass_mgnt.mass_g_peeling;
	mass->quat_after_peeling_oz = mass->quat_after_peeling / 28.3495f;
	mass->quat_after_peeling = roundf(mass->quat_after_peeling * 10.0f) / 10.0f;
	mass->quat_after_peeling_oz = roundf(mass->quat_after_peeling_oz * 10.0f) / 10.0f;

	/************ Flowrate data calculation ************/
	/* Flowrate filtering */
	if(flowratesmooth_enable) {
		if(stable_flag) {
			kalman1d_qflowrate = 0.1f;
		}
		else {
			kalman1d_qflowrate = 0.003f;
		}
		kalman_filter1d_handler_flowrate(&flowrate->raw, &flowrate->filtered);
	}
	else {
		flowrate->filtered = flowrate->raw;
	}
	
	/* Flowrate quantify */
    flowrate->quantified = roundf(flowrate->filtered * 100.0f) / 100.0f;			// two-digits reserved
	flowrate->quantified_1 = floorf(flowrate->quantified * 10.0f) / 10.0f;			// one-digit reserved via floor operation

	/* Mass history buffer update, 0 slice means latest acquired mass */
	mass_history_buf[mass_history_wr_idx] = mass->quat_after_peeling;
	mass_history_wr_idx++;
	if(mass_history_wr_idx >= MASS_MEAS_HISTORY_DELAYLINE_COUNT) {
		mass_history_wr_idx = 0;
	}
	if(mass_history_filled < MASS_MEAS_HISTORY_DELAYLINE_COUNT) {
		mass_history_filled++;
	}
}

void scale_mass_mgnt_init(const float _cali_k, 
                        const float _Ts_s,
                        mass_mgnt_datapack_s * const _flowrate, 
                        mass_mgnt_datapack_s * const _mass,
                        const uint32_t _params_idx) {
	ASSERT(_cali_k != 0);
	ASSERT(_Ts_s != 0);
	ASSERT(_flowrate != NULL);
	ASSERT(_mass != NULL);
	
	if(_params_idx < MASS_MGNT_PARAMS_TABSIZE) {
		/* Use specified parameters */
		sensor_params = sensor_params_tab[_params_idx];
	}
	else {
		/* Use default parameters */
		sensor_params = sensor_params_tab[MASS_MGNT_PARAMS_DEFAULT];
	}
	ASSERT(sensor_params != NULL);
	
	/* Resource reset */
	mass_mgnt_wholereset();
	rtt_mass_telemetry_init();
	
	/* Mass manager initialize */
	mass_mgnt.mass_g_cali_k = _cali_k;
	
    /* Bessel2 filter init */
	bessel2_init(&besselfilter,BES_SAMPLE_FREQ,s_filter_cutoff_hz);

    /* sliding stat init */
	sliding_stat_init(&slide,sliding_stat_buf,s_filter_window);

    /* Debug observation chain filter init (directly on g_cs1237_debug_raw) */
	bessel2_init(&bessel_debug,BES_SAMPLE_FREQ,s_filter_cutoff_hz);
	sliding_stat_init(&slide_debug,sliding_stat_buf_debug,s_filter_window);
	/* Driver initialize */
	dev_cs1237_init(cs1237_err_handler);
	if(mass_mgnt.state == HALT) {
		dev_cs1237_sleeping();
	}
	
	mass = _mass;
	flowrate = _flowrate;
	Ts = _Ts_s;
}


ret_code_t scale_mass_mgnt_filter_config_set(float cutoff_hz, uint32_t window) {
	if(!isfinite(cutoff_hz) || cutoff_hz < BES_CUTOFF_FREQ_MIN ||
	   cutoff_hz > BES_CUTOFF_FREQ_MAX || window == 0U ||
	   window > SLIDING_BUFF) {
		return NS_ERROR_INVALID_PARAM;
	}

	taskENTER_CRITICAL();
	s_filter_cutoff_hz = cutoff_hz;
	s_filter_window = window;
	bessel2_init(&besselfilter, BES_SAMPLE_FREQ, s_filter_cutoff_hz);
	bessel2_init(&bessel_debug, BES_SAMPLE_FREQ, s_filter_cutoff_hz);
	sliding_stat_init(&slide, sliding_stat_buf, s_filter_window);
	sliding_stat_init(&slide_debug, sliding_stat_buf_debug, s_filter_window);
	taskEXIT_CRITICAL();

	return NS_SUCCESS;
}


void scale_mass_mgnt_filter_config_get(float *cutoff_hz, uint32_t *window) {
	if(cutoff_hz == NULL || window == NULL) {
		return;
	}

	taskENTER_CRITICAL();
	*cutoff_hz = s_filter_cutoff_hz;
	*window = s_filter_window;
	taskEXIT_CRITICAL();
}


static ret_code_t scale_mass_mgnt_process(bool fatal_on_error,
										  uint32_t stable_sample_count) {
	ret_code_t cs1237_status;
	int32_t sample;
	uint32_t sample_count;

	/* The mass task exclusively owns CS1237 hardware transitions. */
	cs1237_status = dev_cs1237_process();
	if(mass_mgnt.state == HALT) {
		return NS_ERROR_BUSY;
	}
	if(cs1237_status == NS_ERROR_BUSY) {
		return NS_ERROR_BUSY;
	}
	if(cs1237_status != NS_SUCCESS) {
		if(fatal_on_error) {
			APP_ERROR_HANDLER(BOOKOO_ERROR_MASS_WAKEUP_FAIL);
		}
		return cs1237_status;
	}
	if(!dev_cs1237_is_ready()) {
		return NS_ERROR_BUSY;
	}
	if(!dev_cs1237_read_sample(&sample, &sample_count)) {
		return NS_ERROR_INVALID_PARAM;
	}
	if(sample_count == last_sample_count) {
		return NS_ERROR_BUSY;
	}
	last_sample_count = sample_count;
	getraw_and_calcSD(sample, stable_sample_count);
    switch(mass_mgnt.state) {
        case STARTUP: {
			// if(wakeup_jump_cnt < get_startup_settle_count()) {
			// 	wakeup_jump_cnt++;
			// 	cali_offset_reset();
			// 	break;
			// }
            if(false == stable_flag) {
                cali_offset_reset();
                break;
            }
            cali_offset.cnt ++;
            cali_offset.sum += mass_mgnt.mass_g_rawdata;
            
            if(cali_offset.cnt >= cali_offset.cnt_max) {
                mass_mgnt.mass_g_cali_offset = cali_offset.sum / cali_offset.cnt;
                cali_offset_reset();
                mass_mgnt.state = MASS_ACQ;
            }
        } break;
        
        case MASS_ACQ: {
            mass_acq();
        } break;

        case CALI_K_WAITING: {
            if(false == stable_flag) {
                cali_k.cnt = 0;
                cali_k.sum = 0;
                break;
            }
            cali_k.cnt ++;
            cali_k.sum += mass_mgnt.mass_g_rawdata;
            if(cali_k.cnt >= cali_k.cnt_max) {
                cali_k.readypull_val = cali_k.sum / cali_k.cnt;
                cali_k.sum = 0;
                cali_k.cnt = 0;
                mass_mgnt.state = CALI_K_READYPULL;
            }
        } break;

        case CALI_K_READYPULL: {
            if(false == stable_flag) {
                break;
            }
            float delta_val = fabs(rawdata_mean - cali_k.readypull_val);
            /* Find out which standard weight is currently being used */
            for(MASS_STANDARD_WEIGHT_TABLE _use = MASS_STANDARD_100G; _use < MASS_STANDARD_WEIGHT_COUNT; _use ++) {
                 if( (delta_val > (s_standard_weight_table[_use] * (1.0f - sensor_params->calik_fluctuation))) && 
                    (delta_val < (s_standard_weight_table[_use] * (1.0f + sensor_params->calik_fluctuation)))) {
                    s_cali_standard_use = _use;
                    mass_mgnt.state = CALI_K_PULLWAITING;
						SCALE_MASS_LOG_INFO("Identify standard weight: %f g, start calibration", s_standard_weight_table[_use]);
                    break;
                }
            }
        } break;
        
        case CALI_K_PULLWAITING: {
            if(false == stable_flag) {
                cali_k.cnt = 0;
                cali_k.sum = 0;
                mass_mgnt.state = CALI_K_READYPULL;
                break;
            }
            cali_k.cnt ++;
            cali_k.sum += (rawdata_mean - cali_k.readypull_val);
            if(cali_k.cnt >= cali_k.cnt_max) {
                /* Ratio calculation */
                float temp = cali_k.sum / cali_k.cnt;
                mass_mgnt.mass_g_cali_k = s_standard_weight_table[s_cali_standard_use] / temp;
                mass_mgnt.state = CALI_K_SUCCESS;
                cali_k.cnt = 0;
				SCALE_MASS_LOG_INFO("Calibration success!");
            }
        } break;

        case CALI_K_SUCCESS: {
            cali_k.cnt++;
            if(cali_k.cnt >= (cali_k.cnt_max / 2)) {
                cali_k_reset();
                mass_calc_core();
                mass_mgnt.mass_g_peeling += (mass_aftercomp - mass_mgnt.mass_g_peeling) - s_standard_weight_table[s_cali_standard_use];
                mass_core_reset();
                mass_mgnt.state = MASS_ACQ;
            }
        } break;

        case PEELING: {
			scale_peeling_estimate_t estimate = { 0 };

            mass_calc_core();
            mass_aftercomp = mass->filtered - zerocomp;
			mass->absolute_mass = mass_aftercomp;
			scale_peeling_session_push(&peeling_session, mass_aftercomp);

			if(stable_flag) {
				peeling_complete(mass_aftercomp, NULL, false);
                break;
            }

			if(scale_peeling_session_timed_out(&peeling_session)) {
				if(!scale_peeling_session_estimate(&peeling_session, &estimate)) {
					estimate.value = mass_aftercomp;
					estimate.mode = SCALE_PEELING_ESTIMATE_LATEST;
					estimate.trend_delta = 0.0f;
					estimate.residual = 0.0f;
				}
				peeling_complete(estimate.value, &estimate, true);
				break;
			}

			peeling_output_update();
			manual_peeling_output_reset();
        } break;
        
        default: break;
    }; // END SWITCH
	return NS_SUCCESS;
}

void scale_mass_mgnt_processing(void) {
	(void)scale_mass_mgnt_process(true, MASS_MGNT_RAWDATA_STAT_WIN);
}

ret_code_t scale_mass_mgnt_probe_processing(void)
{
	return scale_mass_mgnt_process(false, SLEEP_PROBE_STABLE_SAMPLE_COUNT);
}

ret_code_t scale_mass_mgnt_probe_pause(void)
{
	return dev_cs1237_acquisition_pause();
}

ret_code_t scale_mass_mgnt_probe_resume(void)
{
	mass_mgnt_acquisition_window_reset();
	last_sample_count = dev_cs1237_sample_count();
	if(!dev_cs1237_is_ready()) {
		return dev_cs1237_wakeup();
	}
	return dev_cs1237_acquisition_resume();
}

ret_code_t scale_mass_mgnt_probe_wakeup(void)
{
	ret_code_t status;

	if(!dev_cs1237_is_ready()) {
		status = dev_cs1237_wakeup();
		if((status != NS_SUCCESS) && (status != NS_ERROR_BUSY)) {
			return status;
		}
		status = dev_cs1237_process();
		if((status != NS_SUCCESS) || !dev_cs1237_is_ready()) {
			return (status == NS_SUCCESS) ? NS_ERROR_BUSY : status;
		}
	}
	mass_mgnt_acquisition_window_reset();
	last_sample_count = dev_cs1237_sample_count();
	return dev_cs1237_acquisition_resume();
}

ret_code_t scale_mass_mgnt_probe_power_down(void)
{
	ret_code_t status = dev_cs1237_acquisition_pause();

	if(status == NS_ERROR_BUSY) {
		return status;
	}
	if(!dev_cs1237_is_sleeping()) {
		dev_cs1237_sleeping();
		status = dev_cs1237_process();
	}
	return dev_cs1237_is_sleeping() ? NS_SUCCESS : status;
}

ret_code_t scale_mass_mgnt_probe_restore_normal(void)
{
	return scale_mass_mgnt_probe_wakeup();
}

bool scale_mass_mgnt_stop_blocked(void)
{
	return dev_cs1237_transfer_active();
}

bool scale_mass_mgnt_normal_ready(void)
{
	return dev_cs1237_is_ready();
}

uint32_t scale_mass_mgnt_sample_count(void)
{
	return dev_cs1237_sample_count();
}

ret_code_t scale_mass_mgnt_explicit_wake_startup(void)
{
	ret_code_t status = dev_cs1237_acquisition_pause();

	if(status == NS_ERROR_BUSY) {
		return status;
	}
	if((status != NS_SUCCESS) && (status != NS_ERROR_INVALID_STATE)) {
		return status;
	}
	mass_mgnt_wholereset();
	mass_mgnt.state = STARTUP;
	if(!dev_cs1237_is_ready()) {
		return dev_cs1237_wakeup();
	}
	return dev_cs1237_acquisition_resume();
}


void scale_mass_mgnt_req(mass_mgnt_req_e const _req) {
	switch(_req) {
		/* HALT_REQ has the highist priority */
		case HALT_REQ: dev_cs1237_sleeping(); mass_mgnt.state = HALT; mass_mgnt_wholereset(); break;
		case STARTUP_REQ: {
			if(mass_mgnt.state == HALT) {
				mass_mgnt.state = STARTUP;
				if(dev_cs1237_wakeup() != NS_SUCCESS) {
					APP_ERROR_HANDLER(BOOKOO_ERROR_MASS_WAKEUP_FAIL);
				}
			}
		} break;
			
		case CALI_K_REQ: {
			if(mass_mgnt.state == PEELING) {
				peeling_cancel();
			}
			if(mass_mgnt.state == MASS_ACQ) {
				mass_mgnt.state = CALI_K_WAITING;
			}
		} break;
			
		case CALI_K_EXIT_REQ: {
			if((mass_mgnt.state >= CALI_K_WAITING)) {
				cali_k_reset();
				mass_mgnt.state = MASS_ACQ;
			}
		} break;
			
		case PEELING_REQ: {
			if(mass_mgnt.state == MASS_ACQ) {
				if(scale_peeling_session_start(&peeling_session, mass_mgnt.mass_g_peeling)) {
					mass_mgnt.state = PEELING;
					manual_peeling_output_reset();
				}
			}
		} break;
			
		case PEELING_EXIT_REQ: {
			if(mass_mgnt.state == PEELING) {
				peeling_cancel();
			}
		} break;

		case DIRECTLY_PEELING_REQ: {
			if((mass_mgnt.state == MASS_ACQ) && stable_flag) {
				mass_mgnt.mass_g_peeling = mass_aftercomp;
				peeling_output_update();
			}
		} break;
		
	};	// END SWITCH
}


mass_mgnt_state_e scale_mass_mgnt_stateget(void) {
	return mass_mgnt.state;
}


float scale_mass_mgnt_caliKget(void) {
	return mass_mgnt.mass_g_cali_k;
}


bool scale_mass_mgnt_stableget(void) {
	return stable_flag;
}


/*switch sensor parameter*/
void scale_mass_mgnt_switchsensor(const uint32_t _params_idx) {
	if(_params_idx < MASS_MGNT_PARAMS_TABSIZE) {
		/* Use specified parameters */
		sensor_params = sensor_params_tab[_params_idx];
	}
	else{
		/* Use default parameters */
		sensor_params = sensor_params_tab[MASS_MGNT_PARAMS_DEFAULT];
	}
	/*reset parameters*/
    cali_k_reset();
	cali_offset_reset();
}


void scale_mass_mgnt_flowratesmooth(bool _enable) {
    flowratesmooth_enable = _enable;
}

bool is_mass_standby_stable(void) {
    return standby_stable_flag;
}


void scale_mass_mgnt_history_mass_get(float *_mass_g) {
	ASSERT(_mass_g != NULL);

	if(mass_history_filled == 0U) {
		*_mass_g = 0.0f;
		return;
	}

	uint32_t latest_idx = (mass_history_wr_idx + MASS_MEAS_HISTORY_DELAYLINE_COUNT - 1U) % MASS_MEAS_HISTORY_DELAYLINE_COUNT;
	uint32_t slice_ago = MASS_MEAS_HISTORY_DELAY_COUNT;
	if(slice_ago >= mass_history_filled) {
		slice_ago = mass_history_filled - 1U;
	}

	uint32_t target_idx = (latest_idx + MASS_MEAS_HISTORY_DELAYLINE_COUNT - slice_ago) % MASS_MEAS_HISTORY_DELAYLINE_COUNT;
	*_mass_g = mass_history_buf[target_idx];
}
/**
 * @}
 */

#ifndef __DEV_VARS_TABLE_H__
#define __DEV_VARS_TABLE_H__

#include "bookoo_error_def.h"
#include "util.h"
#include "default_parameters.h"


typedef struct {
	FORMAT_4BYTES_U value;
	const VAR_TYPE_E type;
} vars_item_t;


typedef enum {
	/* Firmware info */
    SOFT_VERSION_INDEX = 0,
    COMPILE_VERSION_INDEX,
    LATEST_COMMIT_TS_INDEX,
    LATEST_COMPILE_TS_INDEX,
    LATEST_AUTHOR_INDEX,

	/* Chip hardware info */
    CHIP_UID_LO_INDEX,
    CHIP_UID_HI_INDEX,
    EXT_FLASH_UID_LO_INDEX,
    EXT_FLASH_UID_HI_INDEX,
    EXT_FLASH_WRITE_COUNT_INDEX,
	
    /* mass data */
    POWER_PERCENT_INDEX,
    MASS_AFTER_PEEL_INDEX,
    FLOWRATE_RAW_INDEX,
    FLOWRATE_QUANTIFIED_INDEX,
    FLOWRATE_QUANTIFIED1_INDEX,
    TIMING_MS_INDEX,
    AUTO_MASS_INDEX,
    AUTO_FLOWRATE_INDEX,
    AUTO_TIMING_MS_INDEX,
	MASS_ABSOLUTE_INDEX,
    
    /*In FSM/FSMreq shared variables */
    FSM_AUTO_CUP_PLACED,
    FSM_AUTO_FIRSTSTOP_TRIGGED,
    FSM_AUTO_MODE_ENDREQ,
    FSM_AUTO_MASS,
    FSM_AUTO_FLOWRATE,
    FSM_AUTO_TIMING_MS,
    FSM_AUTO_READY_PEELINGWAIT_CNT,
    FSM_AUTO_MODE_SWITCHCNT,
    FSM_LAST_PEELINGMASS,
    FSM_DISP_TEST_COUNT,
    FSM_STANDBY_COUNT,
    FSM_AUTO_TRIG_MASK_TS,
	
    
#ifdef DEBUG_BLEVOFA
/* vofa debug add */
    DEBUG_MASS_RAW,
    DEBUG_MASS_FIR_RAW,
    DEBUG_MASS_RAWSTD,
    DEBUG_MASS_FIR_STD,
    DEBUG_MASS_ZEROCOMP,
    DEBUG_MASS_AFTERCOMP,
    DEBUG_MASS_QUANTIFIED,
/* vofa debug add */
#endif
	VARS_TOTAL_COUNT
} VARS_CACHE_INDEX_E;

SYSTEM_ERROR_CODE_E dev_vars_table_init(void);

void dev_vars_table_deinit(void);

SYSTEM_ERROR_CODE_E dev_vars_table_set(VARS_CACHE_INDEX_E index, FORMAT_4BYTES_U *value);

SYSTEM_ERROR_CODE_E dev_vars_table_get(VARS_CACHE_INDEX_E index, FORMAT_4BYTES_U *out_value);

int dev_vars_table_get_type(VARS_CACHE_INDEX_E index, uint32_t timeout);

#endif // __DEV_VARS_TABLE_H__

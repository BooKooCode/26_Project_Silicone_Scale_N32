#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "default_parameters.h"
#include "dev_vars_table.h"
#include "log.h"

typedef struct {
    vars_item_t *items;
    uint8_t item_count;
    SemaphoreHandle_t mutex;
} dev_vars_sub_table_t;

typedef enum {
    DEV_VARS_TABLE_INVALID = 0,
    DEV_VARS_TABLE_FW,
    DEV_VARS_TABLE_CHIP_HW,
    DEV_VARS_TABLE_MASS,
    DEV_VARS_TABLE_FSM,
#ifdef DEBUG_BLEVOFA
DEV_VARS_TABLE_DEBUG,
#endif    
    DEV_VARS_TABLE_COUNT,
} dev_vars_table_id_t;

typedef struct {
    uint8_t table_id;
    uint8_t subindex;
} dev_vars_index_map_t;


#define DEV_VARS_TABLE_LOG_ERROR(...)    LOG_ERROR("dev_vars_table", __VA_ARGS__)


static vars_item_t s_firmware_vars[] = {
    {.type = US32_TYPE},            /* SOFT_VERSION_INDEX */
    {.type = US32_TYPE},            /* COMPILE_VERSION_INDEX */
    {.type = US32_TYPE},            /* LATEST_COMMIT_TS_INDEX */
    {.type = US32_TYPE},            /* LATEST_COMPILE_TS_INDEX */
    {.type = US32_TYPE},            /* LATEST_AUTHOR_INDEX */
};

static vars_item_t s_chip_hw_vars[] = {
    {.type = US32_TYPE},            /* CHIP_UID_LO_INDEX */
    {.type = US32_TYPE},            /* CHIP_UID_HI_INDEX */
    {.type = US32_TYPE},            /* EXT_FLASH_UID_LO_INDEX */
    {.type = US32_TYPE},            /* EXT_FLASH_UID_HI_INDEX */
    {.type = US32_TYPE},            /* EXT_FLASH_WRITE_COUNT_INDEX */
};

static vars_item_t s_mass_vars[] = {
    {.type = US8_TYPE},             /* POWER_PERCENT_INDEX */
    {.type = FL32_TYPE},            /* MASS_AFTER_PEEL_INDEX */
    {.type = FL32_TYPE},            /* FLOWRATE_RAW_INDEX */
    {.type = FL32_TYPE},            /* FLOWRATE_QUANTIFIED_INDEX */
    {.type = FL32_TYPE},            /* FLOWRATE_QUANTIFIED1_INDEX */
    {.type = US32_TYPE},            /* TIMING_MS_INDEX */
    {.type = FL32_TYPE},            /* AUTO_MASS_INDEX */
    {.type = FL32_TYPE},            /* AUTO_FLOWRATE_INDEX */
    {.type = US32_TYPE},            /* AUTO_TIMING_MS_INDEX */
	{.type = FL32_TYPE},            /* MASS_ABSOLUTE_INDEX */
};

static vars_item_t s_fsm_vars[] = {
    {.type = US8_TYPE},             /* FSM_AUTO_CUP_PLACED */
    {.type = US8_TYPE},             /* FSM_AUTO_FIRSTSTOP_TRIGGED */
    {.type = US8_TYPE},             /* FSM_AUTO_MODE_ENDREQ */
    {.type = FL32_TYPE},            /* FSM_AUTO_MASS */
    {.type = FL32_TYPE},            /* FSM_AUTO_FLOWRATE */
    {.type = US32_TYPE},            /* FSM_AUTO_TIMING_MS */
    {.type = US8_TYPE},             /* FSM_AUTO_READY_PEELINGWAIT_CNT */
    {.type = US32_TYPE},            /* FSM_AUTO_MODE_SWITCHCNT */
    {.type = FL32_TYPE},            /* FSM_LAST_PEELINGMASS */
    {.type = US32_TYPE},            /* FSM_DISP_TEST_COUNT */
    {.type = US32_TYPE},            /* FSM_STANDBY_COUNT */
    {.type = US32_TYPE},            /* FSM_AUTO_TRIG_MASK_TS */
};

#ifdef DEBUG_BLEVOFA
static vars_item_t s_debug_vars[] = {
    {.type = FL32_TYPE},            /* DEBUG_MASS_RAW */
    {.type = FL32_TYPE},            /* DEBUG_MASS_FIR_RAW */
    {.type = FL32_TYPE},            /* DEBUG_MASS_RAWSTD */
    {.type = FL32_TYPE},            /* DEBUG_MASS_FIR_RAWSTD */
    {.type = FL32_TYPE},            /* DEBUG_MASS_ZEROCOMP */
    {.type = FL32_TYPE},            /* DEBUG_MASS_AFTERCOMP */
    {.type = FL32_TYPE},            /* DEBUG_MASS_QUANTIFIED */
};
#endif

static dev_vars_sub_table_t s_vars_tables[] = {
    {
        .items = s_firmware_vars,
        .item_count = (uint8_t)(sizeof(s_firmware_vars) / sizeof(s_firmware_vars[0])),
        .mutex = NULL,
    },
    {
        .items = s_chip_hw_vars,
        .item_count = (uint8_t)(sizeof(s_chip_hw_vars) / sizeof(s_chip_hw_vars[0])),
        .mutex = NULL,
    },
    {
        .items = s_mass_vars,
        .item_count = (uint8_t)(sizeof(s_mass_vars) / sizeof(s_mass_vars[0])),
        .mutex = NULL,
    },
    {
        .items = s_fsm_vars,
        .item_count = (uint8_t)(sizeof(s_fsm_vars) / sizeof(s_fsm_vars[0])),
        .mutex = NULL,
    },
#ifdef DEBUG_BLEVOFA
    {
        .items = s_debug_vars,,
        .item_count = (uint8_t)(sizeof(s_control_vars) / sizeof(s_control_vars[0])),
        .mutex = NULL,
    },
#endif
};


static const dev_vars_index_map_t s_vars_index_map[VARS_TOTAL_COUNT] = {
    [SOFT_VERSION_INDEX]        = {.table_id = DEV_VARS_TABLE_FW, .subindex = 0},
    [COMPILE_VERSION_INDEX]     = {.table_id = DEV_VARS_TABLE_FW, .subindex = 1},
    [LATEST_COMMIT_TS_INDEX]    = {.table_id = DEV_VARS_TABLE_FW, .subindex = 2},
    [LATEST_COMPILE_TS_INDEX]   = {.table_id = DEV_VARS_TABLE_FW, .subindex = 3},
    [LATEST_AUTHOR_INDEX]       = {.table_id = DEV_VARS_TABLE_FW, .subindex = 4},


    [CHIP_UID_LO_INDEX]         = {.table_id = DEV_VARS_TABLE_CHIP_HW, .subindex = 0},
    [CHIP_UID_HI_INDEX]         = {.table_id = DEV_VARS_TABLE_CHIP_HW, .subindex = 1},
    [EXT_FLASH_UID_LO_INDEX]    = {.table_id = DEV_VARS_TABLE_CHIP_HW, .subindex = 2},
    [EXT_FLASH_UID_HI_INDEX]    = {.table_id = DEV_VARS_TABLE_CHIP_HW, .subindex = 3},
    [EXT_FLASH_WRITE_COUNT_INDEX] = {.table_id = DEV_VARS_TABLE_CHIP_HW, .subindex = 4},

    [POWER_PERCENT_INDEX]       = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 0},
    [MASS_AFTER_PEEL_INDEX]     = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 1},
    [FLOWRATE_RAW_INDEX]        = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 2},
    [FLOWRATE_QUANTIFIED_INDEX] = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 3},
    [FLOWRATE_QUANTIFIED1_INDEX]= {.table_id = DEV_VARS_TABLE_MASS, .subindex = 4},
    [TIMING_MS_INDEX]           = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 5},
    [AUTO_MASS_INDEX]           = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 6},
    [AUTO_FLOWRATE_INDEX]       = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 7},
    [AUTO_TIMING_MS_INDEX]      = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 8},
	[MASS_ABSOLUTE_INDEX]         = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 9},

    [FSM_AUTO_CUP_PLACED]         = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 0},
    [FSM_AUTO_FIRSTSTOP_TRIGGED]  = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 1},
    [FSM_AUTO_MODE_ENDREQ]        = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 2},
    [FSM_AUTO_MASS]               = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 3},
    [FSM_AUTO_FLOWRATE]           = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 4},
    [FSM_AUTO_TIMING_MS]          = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 5},
    [FSM_AUTO_READY_PEELINGWAIT_CNT] = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 6},
    [FSM_AUTO_MODE_SWITCHCNT]     = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 7},
    [FSM_LAST_PEELINGMASS]        = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 8},
    [FSM_DISP_TEST_COUNT]         = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 9},
    [FSM_STANDBY_COUNT]           = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 10},
    [FSM_AUTO_TRIG_MASK_TS]        = {.table_id = DEV_VARS_TABLE_FSM, .subindex = 11},
//    [FSM_IS_INCHARGE]             = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 11},
//    [FSM_GRAM_AFTER_PEEL]         = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 12},
//    [FSM_FLOWRATE_RAW]            = {.table_id = DEV_VARS_TABLE_MASS, .subindex = 13},
    
#ifdef DEBUG_BLEVOFA
    [DEBUG_MASS_RAW]        = {.table_id = DEV_VARS_TABLE_DEBUG, .subindex = 0},
    [DEBUG_MASS_FIR_RAW]    = {.table_id = DEV_VARS_TABLE_DEBUG, .subindex = 1},
    [DEBUG_MASS_RAWSTD]     = {.table_id = DEV_VARS_TABLE_DEBUG, .subindex = 2},
    [DEBUG_MASS_FIR_STD]    = {.table_id = DEV_VARS_TABLE_DEBUG, .subindex = 3},
    [DEBUG_MASS_ZEROCOMP]   = {.table_id = DEV_VARS_TABLE_DEBUG, .subindex = 4},
    [DEBUG_MASS_AFTERCOMP]  = {.table_id = DEV_VARS_TABLE_DEBUG, .subindex = 5},
    [DEBUG_MASS_QUANTIFIED] = {.table_id = DEV_VARS_TABLE_DEBUG, .subindex = 6},
#endif    
   
};


static SYSTEM_ERROR_CODE_E dev_vars_table_self_check(void) {
    if ((sizeof(s_vars_tables) / sizeof(s_vars_tables[0])) != (DEV_VARS_TABLE_COUNT - 1)) {
        DEV_VARS_TABLE_LOG_ERROR("Vars table count mismatch!");
        return ERR_FAIL;
    }
    for (uint32_t i = 0; i < VARS_TOTAL_COUNT; i++) {
        const dev_vars_index_map_t *map = &s_vars_index_map[i];
        if (map->table_id == DEV_VARS_TABLE_INVALID) {
            DEV_VARS_TABLE_LOG_ERROR("Vars index map missing entry! %d", i);
            return ERR_FAIL;
        }
        if (map->table_id >= DEV_VARS_TABLE_COUNT) {
            DEV_VARS_TABLE_LOG_ERROR("Vars index map invalid table id! %d", i);
            return ERR_FAIL;
        }
        const dev_vars_sub_table_t *table = &s_vars_tables[map->table_id - 1];
        if (map->subindex >= table->item_count) {
            DEV_VARS_TABLE_LOG_ERROR("Vars index map invalid subindex! %d", i);
            return ERR_FAIL;
        }
    }
    return ERR_NONE;
}


static SYSTEM_ERROR_CODE_E get_sub_table(VARS_CACHE_INDEX_E index, dev_vars_sub_table_t **out_table, uint32_t *out_offset) {
    if (index >= VARS_TOTAL_COUNT) {
        return ERR_INVALID_ARG;
    }
    const dev_vars_index_map_t *map = &s_vars_index_map[index];
    if (map->table_id == DEV_VARS_TABLE_INVALID) {
        return ERR_NOT_SUPPORTED;
    }
    if (map->table_id >= DEV_VARS_TABLE_COUNT) {
        return ERR_NOT_SUPPORTED;
    }
    *out_table = &s_vars_tables[map->table_id - 1];
    *out_offset = map->subindex;
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E dev_vars_table_init(void) {
    SYSTEM_ERROR_CODE_E check_ret = dev_vars_table_self_check();
    if (check_ret != ERR_NONE) {
        return check_ret;
    }
    for (uint32_t i = 0; i < (sizeof(s_vars_tables) / sizeof(s_vars_tables[0])); i++) {
        s_vars_tables[i].mutex = xSemaphoreCreateMutex();
        if (s_vars_tables[i].mutex == NULL) {
            for (uint32_t j = 0; j < i; j++) {
                vSemaphoreDelete(s_vars_tables[j].mutex);
                s_vars_tables[j].mutex = NULL;
            }
            return ERR_NO_MEM;
        }
    }
    return ERR_NONE;
}


void dev_vars_table_deinit(void) {
    for (uint32_t i = 0; i < (sizeof(s_vars_tables) / sizeof(s_vars_tables[0])); i++) {
        if (s_vars_tables[i].mutex != NULL) {
            vSemaphoreDelete(s_vars_tables[i].mutex);
            s_vars_tables[i].mutex = NULL;
        }
    }
}


SYSTEM_ERROR_CODE_E dev_vars_table_set(VARS_CACHE_INDEX_E index, FORMAT_4BYTES_U *value) {
    if (value == NULL) {
        return ERR_INVALID_POINTER;
    }
    dev_vars_sub_table_t *table = NULL;
    uint32_t offset = 0;
    SYSTEM_ERROR_CODE_E ret = get_sub_table(index, &table, &offset);
    if (ret != ERR_NONE) {
        return ret;
    }
    if (xSemaphoreTake(table->mutex, portMAX_DELAY) != pdTRUE) {
        return ERR_INVALID_STATE;
    }
    do {
        vars_item_t *var = &table->items[offset];
        if (UNDEFINE_TYPE == var->type) {
            ret = ERR_NOT_SUPPORTED;
            break;
        }
        memcpy(&var->value, value, sizeof(FORMAT_4BYTES_U));
        ret = ERR_NONE;
    } while (0);
    xSemaphoreGive(table->mutex);
    return ret;
}


SYSTEM_ERROR_CODE_E dev_vars_table_get(VARS_CACHE_INDEX_E index, FORMAT_4BYTES_U *out_value) {
    if (out_value == NULL) {
        return ERR_INVALID_POINTER;
    }
    dev_vars_sub_table_t *table = NULL;
    uint32_t offset = 0;
    SYSTEM_ERROR_CODE_E ret = get_sub_table(index, &table, &offset);
    if (ret != ERR_NONE) {
        return ret;
    }
    if (xSemaphoreTake(table->mutex, portMAX_DELAY) != pdTRUE) {
        return ERR_INVALID_STATE;
    }
    do {
        vars_item_t *var = &table->items[offset];
        if (UNDEFINE_TYPE == var->type) {
            ret = ERR_NOT_SUPPORTED;
            break;
        }
        memcpy(out_value, &var->value, sizeof(FORMAT_4BYTES_U));
        ret = ERR_NONE;
    } while (0);
    xSemaphoreGive(table->mutex);
    return ret;
}


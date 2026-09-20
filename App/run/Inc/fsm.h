#ifndef __FSM_H__
#define __FSM_H__


#include "stdlib.h"
#include "stdint.h"
#include "stdbool.h"
#include "bookoo_error_def.h"


typedef void (*set_global_state_h)(uint32_t);
typedef uint32_t (*get_global_state_h)(void);


typedef struct {
    uint32_t cur_state;
    uint32_t (*pfn_action)(void);
    void (*pfn_enter)(void);
    void (*pfn_exit)(uint32_t);
} fsm_iterate_entry_t;


typedef struct {
    uint32_t cur_state;
    void (*pfn_quest)(void);
} fsm_burst_entry_t;


typedef struct {
    bool new_state;
    uint32_t cache_state;
    const fsm_iterate_entry_t *iter_list;
    uint16_t state_count;
    set_global_state_h set_state;
    get_global_state_h get_state;
} fsm_mgnt_t;


SYSTEM_ERROR_CODE_E fsm_iterate_run(fsm_mgnt_t *_mgnt);


SYSTEM_ERROR_CODE_E fsm_init(fsm_mgnt_t *_mgnt, const fsm_iterate_entry_t *_iter_list, uint16_t _count, set_global_state_h _set_state_handler, get_global_state_h _get_state_handler);


#endif


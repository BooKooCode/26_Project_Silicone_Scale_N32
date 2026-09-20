#include "fsm.h"


SYSTEM_ERROR_CODE_E fsm_iterate_run(fsm_mgnt_t *_mgnt) {
    if(NULL == _mgnt) {
        return ERR_NOT_INIT;
    }
    uint32_t _state = _mgnt->get_state();
    if(_state >= _mgnt->state_count) {
        return ERR_INVALID_ARG;
    }
    const fsm_iterate_entry_t *_item = &_mgnt->iter_list[_state];
    /* Check extern state change */
    if(_state != _mgnt->cache_state) {
        _mgnt->cache_state = _state;
        _mgnt->new_state = true;
    }
    /* Enter Step Init */
    if(_mgnt->new_state) {
        if(NULL != _item->pfn_enter) {
            _item->pfn_enter();
        }
        _mgnt->new_state = false;
    }
    /* Action cycle */
    uint32_t next_state = _state;
    if(_item->pfn_action != NULL) {
        next_state = _item->pfn_action();
    }
    /* Jump to next step */
    if (next_state != _state) {
        if(NULL != _item->pfn_exit) {
            _item->pfn_exit(next_state);
        }
        _mgnt->cache_state = next_state;
        _mgnt->set_state(_mgnt->cache_state);
        _mgnt->new_state = true;
    }
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E fsm_init(fsm_mgnt_t *_mgnt, \
                            const fsm_iterate_entry_t *_iter_list, \
                            uint16_t _count, \
                            set_global_state_h _set_state_handler, \
                            get_global_state_h _get_state_handler) {
                                    
    if(NULL == _mgnt || NULL == _iter_list) {
        return ERR_INVALID_POINTER;
    }
    if(0 == _count) {
        return ERR_INVALID_LEN;
    }
    _mgnt->iter_list = _iter_list;
    _mgnt->state_count = _count;
    _mgnt->set_state = _set_state_handler;
    _mgnt->get_state = _get_state_handler;
    return ERR_NONE;
}


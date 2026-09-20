#ifndef __AUTO_EVENT_HANDLER_H__
#define __AUTO_EVENT_HANDLER_H__

#include "bookoo_error_def.h"
#include <stddef.h>

typedef int (*auto_handle_event_t)(void*);

typedef struct auto_handle_arg *auto_handler;

/**
 * @brief: 
 * @param[in] handle_arg
 * @return {*}
 */
int get_auto_event_handler_member_count(auto_handler handle_arg);

/**
 * @brief: 
 * @param[in] handle_arg
 * @param[in] event_handler
 * @return {*}
 */
SYSTEM_ERROR_CODE_E auto_event_register_handler(auto_handler handle_arg, auto_handle_event_t event_handler);

/**
 * @brief: 
 * @param[in] handle_arg
 * @param[in] event_handler
 * @return {*}
 */
SYSTEM_ERROR_CODE_E auto_event_unregister_handler(auto_handler handle_arg, auto_handle_event_t event_handler);

/**
 * @brief: 
 * @param[in] handle_arg
 * @param[in] input_data
 * @return {*}
 */
SYSTEM_ERROR_CODE_E auto_event_run_handlers(auto_handler handle_arg, void *input_data);

/**
 * @brief: 
 * @param[in] handle_arg
 * @param[in] max_length
 * @return {*}
 */
SYSTEM_ERROR_CODE_E auto_event_manager_init(auto_handler *handle_arg, size_t max_length);


void auto_event_manager_deinit(auto_handler *handle_arg);

#endif

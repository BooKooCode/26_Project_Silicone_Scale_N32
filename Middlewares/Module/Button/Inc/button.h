#ifndef __BUTTON_H__
#define __BUTTON_H__

#include "stdbool.h"
#include "stdint.h"
#include "button_conf.h"
#include "auto_event_handler.h"
#include "double_list.h"
#include "bookoo_error_def.h"


typedef struct button_statistic_t btn_statis_t;

typedef uint8_t (*get_btn_level_handle_t)(uint8_t);

typedef struct {
    btn_statis_t *btn_statis;
    auto_handler *btn_event_handler;
    uint8_t btn_count;
    get_btn_level_handle_t get_btn_level_handler;
} btn_event_mgnt_t;

typedef int (*button_response_handle_t)(void*);


/**
 * @brief Registers a callback handler for button events
 * 
 * @param btn_mgnt Pointer to button event management structure
 * @param button_index Index of the target button (0-based)
 * @param response_handle Callback function to handle button events
 * @return SYSTEM_ERROR_CODE_E Operation status (ERR_NONE on success)
 */
SYSTEM_ERROR_CODE_E register_button_response_event(
    btn_event_mgnt_t *btn_mgnt, 
    uint8_t button_index, 
    button_response_handle_t response_handle
);

/**
 * @brief Unregisters a callback handler from button events
 * 
 * @param btn_mgnt Pointer to button event management structure
 * @param button_index Index of the target button (0-based)
 * @param response_handle Callback function to be removed
 * @return SYSTEM_ERROR_CODE_E Operation status (ERR_NONE on success)
 */
SYSTEM_ERROR_CODE_E unregister_button_response_event(
    btn_event_mgnt_t *btn_mgnt, 
    uint8_t button_index, 
    button_response_handle_t response_handle
);

/**
 * @brief Registers a multi-click event handler (¡Ý3 clicks)
 * 
 * @param btn_mgnt Pointer to button event management structure
 * @param button_index Index of the target button (0-based)
 * @param target_count Number of consecutive clicks to detect (¡Ý3)
 * @return SYSTEM_ERROR_CODE_E Operation status (ERR_NONE on success)
 */
SYSTEM_ERROR_CODE_E register_multi_click_event(
    btn_event_mgnt_t *btn_mgnt, 
    uint8_t button_index, 
    uint8_t target_count, 
    uint8_t valid_level
);

/**
 * @brief Unregisters a multi-click event handler
 * 
 * @param btn_mgnt Pointer to button event management structure
 * @param button_index Index of the target button (0-based)
 * @param target_count Number of clicks to remove from detection
 * @return SYSTEM_ERROR_CODE_E Operation status (ERR_NONE on success)
 */
SYSTEM_ERROR_CODE_E unregister_multi_click_event(
    btn_event_mgnt_t *btn_mgnt, 
    uint8_t button_index, 
    uint8_t target_count
);

/**
 * @brief Updates button state machine (call periodically)
 * 
 * @param btn_mgnt Pointer to button event management structure
 * @param button_index Index of the target button (0-based)
 * @param trigger_level Active trigger level (HIGH/LOW)
 * 
 * @note Must be called at KEY_UPDATE_PER_MS intervals
 *       for accurate timing calculations
 */
void button_state_update(
    btn_event_mgnt_t *btn_mgnt, 
    uint8_t button_index, 
    BUTTON_TRIGGER_LEVEL_E trigger_level
);


void reset_button_startup_state(btn_event_mgnt_t *btn_mgnt, uint8_t button_index);


/**
 * @brief Initializes button event management system
 * 
 * @param btn_mgnt Pointer to uninitialized button management structure
 * @return SYSTEM_ERROR_CODE_E 
 *   - ERR_NONE: Success
 *   - ERR_INVALID_POINTER: Null parameters
 *	 - ERR_INVALID_ARG: Invalid button count
 *   - ERR_REINIT: Already initialized
 *   - ERR_NOT_ALLOCATE: Memory allocation failure
 * 
 * @note Requires pre-configured btn_count and get_btn_level_handler
 */
SYSTEM_ERROR_CODE_E button_service_init(btn_event_mgnt_t *btn_mgnt);


#endif

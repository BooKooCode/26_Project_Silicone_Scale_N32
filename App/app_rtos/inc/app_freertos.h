#ifndef APP_FREERTOS_H
#define APP_FREERTOS_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define TEST_SHUTDOWN_POWER_CUT  0U
#define TEST_SHUTDOWN_KEEP_POWER 1U
#define TEST_SHUTDOWN_CUP_WAKE   2U

#ifndef TEST_SHUTDOWN_MODE
#define TEST_SHUTDOWN_MODE TEST_SHUTDOWN_CUP_WAKE
#endif

#if (TEST_SHUTDOWN_MODE != TEST_SHUTDOWN_POWER_CUT) && \
    (TEST_SHUTDOWN_MODE != TEST_SHUTDOWN_KEEP_POWER) && \
    (TEST_SHUTDOWN_MODE != TEST_SHUTDOWN_CUP_WAKE)
#error "TEST_SHUTDOWN_MODE must be 0, 1 or 2"
#endif

typedef enum {
    TEST_STATE_WAIT_START = 0,
    TEST_STATE_WEIGHING,
    TEST_STATE_CAL_ZERO,
    TEST_STATE_CAL_WEIGHT,
    TEST_STATE_CAL_SAVING,
    TEST_STATE_CAL_DONE,
    TEST_STATE_SLEEPING,
    TEST_STATE_ERROR
} test_state_t;

enum {
    TEST_ERROR_SENSOR = 1,
    TEST_ERROR_ZERO_UNSTABLE = 2,
    TEST_ERROR_WEIGHT_INVALID = 3,
    TEST_ERROR_STORAGE = 4
};

void NS_FREERTOS_Init(void);
test_state_t test_app_state(void);
float test_app_mass_g(void);
uint8_t test_app_error(void);

extern volatile UBaseType_t g_test_controller_stack_min_words;
extern volatile UBaseType_t g_test_mass_stack_min_words;
extern volatile UBaseType_t g_test_display_stack_min_words;
extern volatile int32_t g_test_raw_sample;
extern volatile uint32_t g_test_sample_count;
extern volatile float g_test_sensor_average;
extern volatile uint32_t g_test_sleep_probe_windows;
extern volatile uint32_t g_test_cup_wake_count;
extern volatile uint32_t g_test_stop2_entry_count;
extern volatile uint32_t g_test_stop2_mrf_timeout;

#endif

#include "app_freertos.h"
#include "test_battery.h"
#include "test_cal_store.h"
#include "test_display.h"
#include "dev_buzzer.h"
#include "dev_cs1237.h"
#include "dev_tm1640b.h"
#include "gpio.h"
#include "main.h"
#include "spi.h"
#include "timer_tools.h"
#include <stdbool.h>
#include <stdint.h>

#define CONTROLLER_PERIOD_MS     20U
#define MASS_PERIOD_MS           25U
#define DISPLAY_PERIOD_MS        50U
#define START_HOLD_MS            200U
#define SHUTDOWN_HOLD_MS         1200U
#define SHUTDOWN_GUARD_MS        3000U
#define MULTI_CLICK_WINDOW_MS    300U
#define SENSOR_TIMEOUT_MS        1500U
#define CAL_ZERO_TIMEOUT_MS      15000U
#define CAL_WEIGHT_TIMEOUT_MS    30000U
#define CAL_DONE_MS              1000U
#define ERROR_DISPLAY_MS         2000U
#define STACK_SAMPLE_MS          1000U
#define STABLE_SAMPLE_COUNT      20U
#define CAL_AVERAGE_COUNT        80U
#define DEFAULT_CALIBRATION      (-1.0f)
#define RAW_TO_GRAMS             (3000.0f / (1073741.824f * 2.0f))

static volatile test_state_t state = TEST_STATE_WAIT_START;
static volatile float displayed_mass;
static volatile uint8_t error_code;
static volatile bool shutdown_pending;
static float calibration = DEFAULT_CALIBRATION;
static float zero_value;
static bool zero_ready;

static StaticTask_t controller_tcb;
static StaticTask_t mass_tcb;
static StaticTask_t display_tcb;
static StackType_t controller_stack[192U];
static StackType_t mass_stack[256U];
static StackType_t display_stack[160U];
static TaskHandle_t controller_handle;
static TaskHandle_t mass_handle;
static TaskHandle_t display_handle;

volatile UBaseType_t g_test_controller_stack_min_words;
volatile UBaseType_t g_test_mass_stack_min_words;
volatile UBaseType_t g_test_display_stack_min_words;
volatile int32_t g_test_raw_sample;
volatile uint32_t g_test_sample_count;
volatile float g_test_sensor_average;

static float stable_samples[STABLE_SAMPLE_COUNT];
static uint32_t stable_index;
static uint32_t stable_count;
static float cal_sum;
static uint32_t cal_count;
static TickType_t state_started;

static bool left_pressed(void)
{
    return GPIO_ReadInputDataBit(btn_left_GPIO_Port, btn_left_Pin) == Bit_RESET;
}

static bool right_pressed(void)
{
    return GPIO_ReadInputDataBit(btn_right_GPIO_Port, btn_right_Pin) != Bit_RESET;
}

static bool debounce_right(bool raw, bool *candidate, bool *stable, uint8_t *count)
{
    if (raw != *candidate) {
        *candidate = raw;
        *count = 1U;
    } else if (*count < 2U) {
        (*count)++;
    }
    if (*count >= 2U) {
        *stable = *candidate;
    }
    return *stable;
}

static void power_off(void)
{
    uint8_t battery_percent;

    dev_buzzer_disable();
    dev_cs1237_sleeping();
    vTaskSuspend(display_handle);
    battery_percent = test_battery_percent();
    test_display_show_battery(battery_percent);
    vTaskDelay(pdMS_TO_TICKS(800U));
    vTaskSuspendAll();
    test_display_shutdown();
    GPIO_ResetBits(HW_POWERON_GPIO_Port, HW_POWERON_Pin);
    __DSB();
    for (;;) {
        __WFI();
    }
}

static void set_error(uint8_t code)
{
    error_code = code;
    state = TEST_STATE_ERROR;
    state_started = xTaskGetTickCount();
}

static void stable_reset(void)
{
    stable_index = 0U;
    stable_count = 0U;
    cal_sum = 0.0f;
    cal_count = 0U;
}

static bool stable_push(float value, float *average)
{
    float minimum;
    float maximum;
    float sum = 0.0f;

    stable_samples[stable_index] = value;
    stable_index = (stable_index + 1U) % STABLE_SAMPLE_COUNT;
    if (stable_count < STABLE_SAMPLE_COUNT) {
        stable_count++;
    }
    minimum = stable_samples[0];
    maximum = stable_samples[0];
    for (uint32_t index = 0U; index < stable_count; index++) {
        float sample = stable_samples[index];
        sum += sample;
        if (sample < minimum) {
            minimum = sample;
        }
        if (sample > maximum) {
            maximum = sample;
        }
    }
    *average = sum / (float)stable_count;
    if (stable_count < STABLE_SAMPLE_COUNT) {
        return false;
    }
    return (maximum - minimum) <= 0.5f;
}

static void begin_calibration(void)
{
    if (state != TEST_STATE_WEIGHING) {
        return;
    }
    stable_reset();
    state = TEST_STATE_CAL_ZERO;
    state_started = xTaskGetTickCount();
}

static void controller_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t boot_tick = last_wake;
    TickType_t left_started = 0U;
    TickType_t last_click = 0U;
    TickType_t last_stack_sample = last_wake;
    bool previous_right = false;
    bool right_candidate = false;
    bool right_stable = false;
    uint8_t right_debounce_count = 0U;
    bool startup_accepted = false;
    bool shutdown_armed = false;
    uint8_t click_count = 0U;

    (void)argument;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        bool left = left_pressed();
        bool right = debounce_right(right_pressed(), &right_candidate, &right_stable,
                                    &right_debounce_count);

        if (!startup_accepted) {
            if (left) {
                if (left_started == 0U) {
                    left_started = now;
                } else if ((now - left_started) >= pdMS_TO_TICKS(START_HOLD_MS)) {
                    startup_accepted = true;
                    state = TEST_STATE_WEIGHING;
                    state_started = now;
                    (void)dev_cs1237_wakeup();
                    (void)dev_tm1640b_wakeup();
                    dev_buzzer_action(SONG_STARTUP);
                }
            } else if (left_started != 0U) {
                power_off();
            }
        } else {
            if (!left) {
                left_started = 0U;
                shutdown_armed = true;
            } else if (shutdown_armed && left_started == 0U) {
                left_started = now;
            } else if (shutdown_armed &&
                       (now - boot_tick) >= pdMS_TO_TICKS(SHUTDOWN_GUARD_MS) &&
                       (now - left_started) >= pdMS_TO_TICKS(SHUTDOWN_HOLD_MS)) {
                if (state == TEST_STATE_CAL_SAVING) {
                    shutdown_pending = true;
                    shutdown_armed = false;
                    left_started = 0U;
                } else {
                    power_off();
                }
            }

            if (previous_right && !right) {
                if (click_count == 0U || (now - last_click) <= pdMS_TO_TICKS(MULTI_CLICK_WINDOW_MS)) {
                    click_count++;
                } else {
                    click_count = 1U;
                }
                last_click = now;
                if (click_count > 8U) {
                    click_count = 0U;
                }
            }
            if (click_count != 0U &&
                (now - last_click) > pdMS_TO_TICKS(MULTI_CLICK_WINDOW_MS)) {
                if (click_count == 8U) {
                    dev_buzzer_action(SONG_PRESSBTN);
                    begin_calibration();
                }
                click_count = 0U;
            }
        }
        if ((now - last_stack_sample) >= pdMS_TO_TICKS(STACK_SAMPLE_MS)) {
            g_test_controller_stack_min_words = uxTaskGetStackHighWaterMark(controller_handle);
            g_test_mass_stack_min_words = uxTaskGetStackHighWaterMark(mass_handle);
            g_test_display_stack_min_words = uxTaskGetStackHighWaterMark(display_handle);
            last_stack_sample = now;
        }
        previous_right = right;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONTROLLER_PERIOD_MS));
    }
}

static float identify_standard(float delta)
{
    static const float standards[] = {100.0f, 500.0f, 1000.0f};

    if (delta < 0.0f) {
        delta = -delta;
    }
    for (uint32_t index = 0U; index < sizeof(standards) / sizeof(standards[0]); index++) {
        if (delta > standards[index] * 0.85f && delta < standards[index] * 1.15f) {
            return standards[index];
        }
    }
    return 0.0f;
}

static void process_calibration(float sample, float stable_average)
{
    TickType_t now = xTaskGetTickCount();

    if (state == TEST_STATE_CAL_ZERO) {
        zero_value = stable_average;
        stable_reset();
        state = TEST_STATE_CAL_WEIGHT;
        state_started = now;
        return;
    }
    if (state == TEST_STATE_CAL_WEIGHT) {
        float delta = stable_average - zero_value;
        float standard = identify_standard(delta);

        if (standard == 0.0f) {
            cal_sum = 0.0f;
            cal_count = 0U;
            return;
        }
        cal_sum += sample - zero_value;
        cal_count++;
        if (cal_count >= CAL_AVERAGE_COUNT) {
            float averaged_delta = cal_sum / (float)cal_count;
            float new_calibration = standard / averaged_delta;

            state = TEST_STATE_CAL_SAVING;
            if (!test_cal_store_save(new_calibration)) {
                shutdown_pending = false;
                set_error(TEST_ERROR_STORAGE);
                return;
            }
            calibration = new_calibration;
            state = TEST_STATE_CAL_DONE;
            state_started = xTaskGetTickCount();
            if (shutdown_pending) {
                power_off();
            }
        }
    }
}

static void mass_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_sample_tick = last_wake;
    uint32_t previous_sample_count = 0U;

    (void)argument;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        ret_code_t result;
        int32_t raw;
        uint32_t sample_count;

        if (state == TEST_STATE_WAIT_START) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MASS_PERIOD_MS));
            continue;
        }
        if (state == TEST_STATE_ERROR) {
            if ((now - state_started) > pdMS_TO_TICKS(ERROR_DISPLAY_MS)) {
                if (error_code == TEST_ERROR_SENSOR) {
                    dev_cs1237_init(NULL);
                    (void)dev_cs1237_wakeup();
                    last_sample_tick = now;
                }
                stable_reset();
                state = TEST_STATE_WEIGHING;
            }
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MASS_PERIOD_MS));
            continue;
        }
        result = dev_cs1237_process();
        if (result != NS_SUCCESS && result != NS_ERROR_BUSY) {
            set_error(TEST_ERROR_SENSOR);
        }
        if (dev_cs1237_read_sample(&raw, &sample_count) && sample_count != previous_sample_count) {
            float sample = (float)raw * RAW_TO_GRAMS;
            float average;
            bool stable;

            previous_sample_count = sample_count;
            g_test_raw_sample = raw;
            g_test_sample_count = sample_count;
            last_sample_tick = now;
            stable = stable_push(sample, &average);
            g_test_sensor_average = average;
            if (!zero_ready) {
                if (stable_count >= STABLE_SAMPLE_COUNT) {
                    zero_value = average;
                    zero_ready = true;
                    displayed_mass = 0.0f;
                }
            } else {
                displayed_mass = (average - zero_value) * calibration;
            }
            if (stable) {
                if (state == TEST_STATE_CAL_ZERO || state == TEST_STATE_CAL_WEIGHT) {
                    process_calibration(sample, average);
                }
            } else if (state == TEST_STATE_CAL_WEIGHT) {
                cal_sum = 0.0f;
                cal_count = 0U;
            }
        }
        if ((now - last_sample_tick) > pdMS_TO_TICKS(SENSOR_TIMEOUT_MS)) {
            set_error(TEST_ERROR_SENSOR);
        }
        if (state == TEST_STATE_CAL_ZERO &&
            (now - state_started) > pdMS_TO_TICKS(CAL_ZERO_TIMEOUT_MS)) {
            set_error(TEST_ERROR_ZERO_UNSTABLE);
        } else if (state == TEST_STATE_CAL_WEIGHT &&
                   (now - state_started) > pdMS_TO_TICKS(CAL_WEIGHT_TIMEOUT_MS)) {
            set_error(TEST_ERROR_WEIGHT_INVALID);
        } else if (state == TEST_STATE_CAL_DONE &&
                   (now - state_started) > pdMS_TO_TICKS(CAL_DONE_MS)) {
            stable_reset();
            state = TEST_STATE_WEIGHING;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MASS_PERIOD_MS));
    }
}

static void display_task(void *argument)
{
    TickType_t last_wake = xTaskGetTickCount();

    (void)argument;
    for (;;) {
        test_display_update();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
    }
}

void NS_FREERTOS_Init(void)
{
    NS_GPIO_Init();
    hw_delay_tim_init();
    spi_flash_init();
    test_battery_init();
    test_display_init();
    dev_buzzer_init(NULL);
    dev_buzzer_changelevel(20U);
    dev_cs1237_init(NULL);
    (void)test_cal_store_load(&calibration);

    controller_handle = xTaskCreateStatic(controller_task, "control", 192U, NULL, 3U,
                                          controller_stack, &controller_tcb);
    mass_handle = xTaskCreateStatic(mass_task, "mass", 256U, NULL, 2U,
                                   mass_stack, &mass_tcb);
    display_handle = xTaskCreateStatic(display_task, "display", 160U, NULL, 1U,
                                      display_stack, &display_tcb);
    if (controller_handle == NULL || mass_handle == NULL || display_handle == NULL) {
        Error_Handler();
    }
}

test_state_t test_app_state(void)
{
    return state;
}

float test_app_mass_g(void)
{
    return displayed_mass;
}

uint8_t test_app_error(void)
{
    return error_code;
}

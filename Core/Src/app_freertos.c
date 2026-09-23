#include "app_freertos.h"
#include "test_battery.h"
#include "test_cal_store.h"
#include "test_display.h"
#include "dev_buzzer.h"
#include "dev_cs1237.h"
#include "dev_tm1640b.h"
#include "gpio.h"
#include "main.h"
#include "n32l40x_pwr.h"
#include "spi.h"
#include "sleep_probe.h"
#include "timer_tools.h"
#include <math.h>
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

typedef enum {
    TEST_PROBE_IDLE = 0,
    TEST_PROBE_POWERING_DOWN,
    TEST_PROBE_WAITING,
    TEST_PROBE_WAKING,
    TEST_PROBE_SAMPLING,
    TEST_PROBE_RATE_SWITCH_DOWN,
    TEST_PROBE_RATE_SWITCH_WAKING,
    TEST_PROBE_STABILIZING
} test_probe_phase_t;

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
volatile uint32_t g_test_sleep_probe_windows;
volatile uint32_t g_test_cup_wake_count;
#if TEST_SHUTDOWN_MODE == TEST_SHUTDOWN_KEEP_POWER
volatile uint32_t g_test_stop2_mrf_timeout;
#endif

static float stable_samples[STABLE_SAMPLE_COUNT];
static uint32_t stable_index;
static uint32_t stable_count;
static float cal_sum;
static uint32_t cal_count;
static TickType_t state_started;
static sleep_probe_t sleep_probe;
static test_probe_phase_t probe_phase;
static TickType_t probe_powered_down_at;
static TickType_t probe_window_started_at;
static uint32_t probe_initial_sample_count;
static bool probe_driver_error;
static bool probe_window_stable;
static bool probe_wake_candidate;
static float probe_window_average;
static float probe_samples[SLEEP_PROBE_STABLE_SAMPLE_COUNT];
static uint32_t probe_sample_index;
static uint32_t probe_sample_count;

static void set_error(uint8_t code);
static void stable_reset(void);

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

static void hard_power_off(void)
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

#if TEST_SHUTDOWN_MODE == TEST_SHUTDOWN_KEEP_POWER
static __attribute__((noreturn, noinline, section(".RamFunc")))
void retained_power_stop(void)
{
    uint32_t attempts = 1000000U;

    while (((PWR->STS2 & PWR_STS2_MRF) == 0U) && (--attempts != 0U)) {
    }
    if (attempts == 0U) {
        g_test_stop2_mrf_timeout = 1U;
    }

    PWR->CTRL3 = (PWR->CTRL3 & ~(uint32_t)PWR_CTRL3_RAMRETMASK) |
                 PWR_CTRL3_RAM1RET | PWR_CTRL3_RAM2RET;
    PWR->CTRL1 = (PWR->CTRL1 & ~(uint32_t)PWR_CTRL1_LPMSELMASK) |
                 PWR_CTRL1_STOP2;
    SCB->SCR = (SCB->SCR & ~(SCB_SCR_SLEEPONEXIT_Msk |
                             SCB_SCR_SEVONPEND_Msk)) |
               SCB_SCR_SLEEPDEEP_Msk;
    for (;;) {
        __DSB();
        __ISB();
        __WFI();
    }
}

static void retained_power_off(void)
{
    TickType_t deadline;
    uint32_t nvic_word_count;

    dev_buzzer_disable();
    test_display_shutdown();
    vTaskSuspend(display_handle);
    deadline = xTaskGetTickCount() + pdMS_TO_TICKS(100U);
    while (dev_cs1237_power_down() == NS_ERROR_BUSY &&
           (int32_t)(deadline - xTaskGetTickCount()) > 0) {
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
    GPIO_ResetBits(ADC_Control_GPIO_Port, ADC_Control_Pin);
    GPIO_SetBits(W25QXX_NS_GPIO_Port, W25QXX_NS_Pin);
    vTaskSuspendAll();
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    EXTI->IMASK = 0U;
    EXTI->EMASK = 0U;
    EXTI->RT_CFG = 0U;
    EXTI->FT_CFG = 0U;
    EXTI->SWIE = 0U;
    EXTI->PEND = 0x0FFFFFFFU;

    PWR->CTRL2 &= ~(uint32_t)PWR_CTRL2_PVDEN;
    PWR->CTRL3 &= ~(uint32_t)(PWR_CTRL3_WKUP0EN |
                              PWR_CTRL3_WKUP1EN |
                              PWR_CTRL3_WKUP2EN |
                              PWR_CTRL3_IWKUPLEN);
    PWR->STSCLR = PWR_STSCLR_CLRWKUP0 |
                  PWR_STSCLR_CLRWKUP1 |
                  PWR_STSCLR_CLRWKUP2;

    nvic_word_count = (SCnSCB->ICTR & SCnSCB_ICTR_INTLINESNUM_Msk) + 1U;
    if (nvic_word_count > 8U) {
        nvic_word_count = 8U;
    }
    for (uint32_t index = 0U; index < nvic_word_count; index++) {
        NVIC->ICER[index] = UINT32_MAX;
        NVIC->ICPR[index] = UINT32_MAX;
    }

    DBG->CTRL &= ~(DBG_CTRL_SLEEP | DBG_CTRL_STOP | DBG_CTRL_STDBY);
    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    CoreDebug->DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk;
    __DSB();
    __ISB();
    retained_power_stop();
}
#endif

static void probe_stable_reset(void)
{
    probe_sample_index = 0U;
    probe_sample_count = 0U;
}

static bool probe_stable_push(float value, float *average)
{
    float minimum;
    float maximum;
    float sum = 0.0f;

    probe_samples[probe_sample_index] = value;
    probe_sample_index = (probe_sample_index + 1U) % SLEEP_PROBE_STABLE_SAMPLE_COUNT;
    if (probe_sample_count < SLEEP_PROBE_STABLE_SAMPLE_COUNT) {
        probe_sample_count++;
    }
    minimum = probe_samples[0];
    maximum = probe_samples[0];
    for (uint32_t index = 0U; index < probe_sample_count; index++) {
        float sample = probe_samples[index];
        sum += sample;
        if (sample < minimum) {
            minimum = sample;
        }
        if (sample > maximum) {
            maximum = sample;
        }
    }
    *average = sum / (float)probe_sample_count;
    return (probe_sample_count >= SLEEP_PROBE_STABLE_SAMPLE_COUNT) &&
           ((maximum - minimum) <= 0.5f);
}

#if TEST_SHUTDOWN_MODE == TEST_SHUTDOWN_CUP_WAKE
static void begin_sleep_probe(void)
{
    if (state != TEST_STATE_WEIGHING || !zero_ready) {
        return;
    }

    sleep_probe_init(&sleep_probe, true, displayed_mass);
    probe_stable_reset();
    probe_driver_error = false;
    probe_phase = TEST_PROBE_POWERING_DOWN;
    state = TEST_STATE_SLEEPING;
    dev_cs1237_set_probe_rate(true);
    dev_buzzer_disable();
    test_display_shutdown();
    vTaskSuspend(display_handle);
    dev_cs1237_sleeping();
}
#endif

static void shutdown_action(void)
{
#if TEST_SHUTDOWN_MODE == TEST_SHUTDOWN_POWER_CUT
    hard_power_off();
#elif TEST_SHUTDOWN_MODE == TEST_SHUTDOWN_KEEP_POWER
    retained_power_off();
#else
    begin_sleep_probe();
#endif
}

static void finish_sleep_probe(bool sensor_error)
{
    probe_phase = TEST_PROBE_IDLE;
    dev_cs1237_set_probe_rate(false);
    stable_reset();
    if (sensor_error) {
        set_error(TEST_ERROR_SENSOR);
    } else {
        state = TEST_STATE_WEIGHING;
        state_started = xTaskGetTickCount();
        g_test_cup_wake_count++;
    }
    dev_buzzer_enable();
    (void)dev_tm1640b_wakeup();
    vTaskResume(display_handle);
    vTaskResume(controller_handle);
    if (!sensor_error) {
        dev_buzzer_action(SONG_STARTUP);
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
#if TEST_SHUTDOWN_MODE == TEST_SHUTDOWN_KEEP_POWER
                /* Keep-power test mode must not pull HW_POWERON low here. */
                retained_power_off();
#else
                hard_power_off();
#endif
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
                    shutdown_action();
                    left_started = 0U;
                    shutdown_armed = false;
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
        if (state == TEST_STATE_SLEEPING) {
            vTaskSuspend(NULL);
            last_wake = xTaskGetTickCount();
            left_started = 0U;
            shutdown_armed = false;
            previous_right = right_pressed();
            continue;
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
                shutdown_pending = false;
                state = TEST_STATE_WEIGHING;
                shutdown_action();
            }
        }
    }
}

static TickType_t process_sleep_probe(TickType_t now, uint32_t *previous_sample_count)
{
    ret_code_t result;

    switch (probe_phase) {
        case TEST_PROBE_POWERING_DOWN:
            result = dev_cs1237_process();
            if (result != NS_SUCCESS && result != NS_ERROR_BUSY) {
                probe_driver_error = true;
            }
            if (dev_cs1237_is_sleeping()) {
                probe_powered_down_at = now;
                probe_phase = TEST_PROBE_WAITING;
            }
            return pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);

        case TEST_PROBE_WAITING:
            if ((now - probe_powered_down_at) >=
                pdMS_TO_TICKS(SLEEP_PROBE_PERIOD_MS)) {
                result = dev_cs1237_wakeup();
                if (result != NS_SUCCESS && result != NS_ERROR_BUSY) {
                    probe_driver_error = true;
                }
                probe_phase = TEST_PROBE_WAKING;
                return pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);
            }
            return pdMS_TO_TICKS(SLEEP_PROBE_PERIOD_MS) -
                   (now - probe_powered_down_at);

        case TEST_PROBE_WAKING:
            result = dev_cs1237_process();
            if (result == NS_SUCCESS) {
                probe_stable_reset();
                probe_initial_sample_count = dev_cs1237_sample_count();
                *previous_sample_count = probe_initial_sample_count;
                probe_window_stable = false;
                probe_wake_candidate = false;
                probe_window_average = displayed_mass;
                probe_window_started_at = now;
                probe_phase = TEST_PROBE_SAMPLING;
            } else if (result != NS_ERROR_BUSY) {
                sleep_probe_event_e event = sleep_probe_finish_window(
                    &sleep_probe, false, true, false, 0.0f);

                g_test_sleep_probe_windows++;
                probe_driver_error = false;
                if (event == SLEEP_PROBE_EVENT_ERROR_WAKE) {
                    finish_sleep_probe(true);
                } else {
                    dev_cs1237_sleeping();
                    probe_phase = TEST_PROBE_POWERING_DOWN;
                }
            }
            return pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);

        case TEST_PROBE_SAMPLING: {
            int32_t raw;
            uint32_t sample_count;

            result = dev_cs1237_process();
            if (result != NS_SUCCESS && result != NS_ERROR_BUSY) {
                probe_driver_error = true;
            }
            if (dev_cs1237_read_sample(&raw, &sample_count) &&
                sample_count != *previous_sample_count) {
                float mass = ((float)raw * RAW_TO_GRAMS - zero_value) * calibration;
                *previous_sample_count = sample_count;
                g_test_raw_sample = raw;
                g_test_sample_count = sample_count;
                probe_window_average = mass;
                g_test_sensor_average = probe_window_average;
                if (fabsf(mass - sleep_probe_baseline_g(&sleep_probe)) >
                    SLEEP_PROBE_WAKE_DELTA_G) {
                    probe_wake_candidate = true;
                }
                if (probe_wake_candidate) {
                    /* One 640 Hz frame is enough to trigger the quieter 40 Hz
                     * stabilization pass used by normal weighing. */
                    dev_cs1237_set_probe_rate(false);
                    dev_cs1237_sleeping();
                    probe_phase = TEST_PROBE_RATE_SWITCH_DOWN;
                } else {
                    (void)sleep_probe_finish_window(&sleep_probe,
                        dev_cs1237_sample_count() != probe_initial_sample_count,
                        probe_driver_error,
                        false,
                        probe_window_average);
                    g_test_sleep_probe_windows++;
                    probe_driver_error = false;
                    dev_cs1237_sleeping();
                    probe_phase = TEST_PROBE_POWERING_DOWN;
                }
            } else if ((now - probe_window_started_at) >=
                       pdMS_TO_TICKS(SLEEP_PROBE_FIRST_SAMPLE_TIMEOUT_MS)) {
                (void)sleep_probe_finish_window(&sleep_probe,
                    false,
                    probe_driver_error,
                    false,
                    probe_window_average);
                g_test_sleep_probe_windows++;
                probe_driver_error = false;
                dev_cs1237_sleeping();
                probe_phase = TEST_PROBE_POWERING_DOWN;
            }
            return pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);
        }

        case TEST_PROBE_RATE_SWITCH_DOWN:
            result = dev_cs1237_process();
            if (result != NS_SUCCESS && result != NS_ERROR_BUSY) {
                probe_driver_error = true;
            }
            if (dev_cs1237_is_sleeping()) {
                result = dev_cs1237_wakeup();
                if (result != NS_SUCCESS && result != NS_ERROR_BUSY) {
                    probe_driver_error = true;
                }
                probe_phase = TEST_PROBE_RATE_SWITCH_WAKING;
            }
            return pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);

        case TEST_PROBE_RATE_SWITCH_WAKING:
            result = dev_cs1237_process();
            if (result == NS_SUCCESS) {
                probe_phase = TEST_PROBE_STABILIZING;
                probe_window_started_at = now;
                probe_initial_sample_count = dev_cs1237_sample_count();
                *previous_sample_count = probe_initial_sample_count;
                probe_stable_reset();
                probe_window_stable = false;
                probe_driver_error = false;
            } else if (result != NS_ERROR_BUSY) {
                sleep_probe_event_e event = sleep_probe_finish_window(
                    &sleep_probe, false, true, false, 0.0f);

                g_test_sleep_probe_windows++;
                probe_driver_error = false;
                if (event == SLEEP_PROBE_EVENT_ERROR_WAKE) {
                    finish_sleep_probe(true);
                } else {
                    dev_cs1237_set_probe_rate(true);
                    dev_cs1237_sleeping();
                    probe_phase = TEST_PROBE_POWERING_DOWN;
                }
            }
            return pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);

        case TEST_PROBE_STABILIZING: {
            int32_t raw;
            uint32_t sample_count;

            result = dev_cs1237_process();
            if (result != NS_SUCCESS && result != NS_ERROR_BUSY) {
                probe_driver_error = true;
            }
            if (dev_cs1237_read_sample(&raw, &sample_count) &&
                sample_count != *previous_sample_count) {
                float mass = ((float)raw * RAW_TO_GRAMS - zero_value) * calibration;
                *previous_sample_count = sample_count;
                g_test_raw_sample = raw;
                g_test_sample_count = sample_count;
                probe_window_stable = probe_stable_push(mass, &probe_window_average);
                g_test_sensor_average = probe_window_average;
            }
            if ((now - probe_window_started_at) >=
                pdMS_TO_TICKS(SLEEP_PROBE_STABILIZE_WINDOW_MS)) {
                sleep_probe_event_e event = sleep_probe_finish_window(
                    &sleep_probe,
                    dev_cs1237_sample_count() != probe_initial_sample_count,
                    probe_driver_error,
                    probe_window_stable,
                    probe_window_average);

                g_test_sleep_probe_windows++;
                probe_driver_error = false;
                if (event == SLEEP_PROBE_EVENT_CUP_WAKE) {
                    displayed_mass = probe_window_average;
                    finish_sleep_probe(false);
                } else if (event == SLEEP_PROBE_EVENT_ERROR_WAKE) {
                    finish_sleep_probe(true);
                } else {
                    dev_cs1237_set_probe_rate(true);
                    dev_cs1237_sleeping();
                    probe_phase = TEST_PROBE_POWERING_DOWN;
                }
            }
            return pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);
        }

        case TEST_PROBE_IDLE:
        default:
            probe_phase = TEST_PROBE_POWERING_DOWN;
            return pdMS_TO_TICKS(SLEEP_PROBE_SAMPLE_POLL_MS);
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
        if (state == TEST_STATE_SLEEPING) {
            vTaskDelay(process_sleep_probe(now, &previous_sample_count));
            last_wake = xTaskGetTickCount();
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

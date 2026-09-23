#include "test_display.h"
#include "app_freertos.h"
#include "dev_tm1640b.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DISPLAY_BYTES 15U
#define MASS_INDEX     6U
#define UNIT_INDEX     12U
#define DECIMAL_POINT  0x80U
#define UNIT_G         0x04U

static const uint8_t digits[16] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07,
    0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71
};

static void send_buffer(const uint8_t *buffer)
{
    (void)dev_tm1640b_senddata(0U, buffer, DISPLAY_BYTES);
    (void)dev_tm1640b_onoff_ctrl(true);
}

static void show_text(uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t buffer[DISPLAY_BYTES] = {0};
    buffer[MASS_INDEX] = a;
    buffer[MASS_INDEX + 1U] = b;
    buffer[MASS_INDEX + 2U] = c;
    send_buffer(buffer);
}

static void show_error(uint8_t error)
{
    uint8_t buffer[DISPLAY_BYTES] = {0};
    buffer[MASS_INDEX] = digits[14];
    buffer[MASS_INDEX + 1U] = digits[(error / 10U) % 10U];
    buffer[MASS_INDEX + 2U] = digits[error % 10U];
    send_buffer(buffer);
}

static void show_mass(float mass)
{
    uint8_t buffer[DISPLAY_BYTES] = {0};
    bool negative = mass < 0.0f;
    uint32_t value;

    if (negative) {
        mass = -mass;
    }
    if (mass > 3000.0f) {
        mass = 3000.0f;
    }
    if (mass < 1000.0f) {
        value = (uint32_t)(mass * 10.0f + 0.5f);
        buffer[MASS_INDEX + 3U] = digits[value % 10U];
        buffer[MASS_INDEX + 2U] = digits[(value / 10U) % 10U] | DECIMAL_POINT;
        if (value >= 100U) {
            buffer[MASS_INDEX + 1U] = digits[(value / 100U) % 10U];
        }
        if (value >= 1000U) {
            buffer[MASS_INDEX] = digits[(value / 1000U) % 10U];
        } else if (negative) {
            buffer[MASS_INDEX] = 0x40U;
        }
    } else {
        value = (uint32_t)(mass + 0.5f);
        for (uint32_t index = 0U; index < 4U; index++) {
            buffer[MASS_INDEX + 3U - index] = digits[value % 10U];
            value /= 10U;
        }
    }
    buffer[UNIT_INDEX] = UNIT_G;
    send_buffer(buffer);
}

void test_display_init(void)
{
    dev_tm1640b_init();
    dev_tm1640b_setbrightness(3U);
    (void)dev_tm1640b_onoff_ctrl(false);
}

void test_display_update(void)
{
    switch (test_app_state()) {
        case TEST_STATE_WAIT_START:
            (void)dev_tm1640b_onoff_ctrl(false);
            break;
        case TEST_STATE_WEIGHING:
            show_mass(test_app_mass_g());
            break;
        case TEST_STATE_CAL_ZERO:
            show_text(0x39U, 0x77U, 0x38U);
            break;
        case TEST_STATE_CAL_WEIGHT:
            show_text(0x73U, 0x3EU, 0x38U);
            break;
        case TEST_STATE_CAL_SAVING:
            show_text(0x6DU, 0x77U, 0x3EU);
            break;
        case TEST_STATE_CAL_DONE:
            show_text(0x5EU, 0x5CU, 0x37U);
            break;
        case TEST_STATE_SLEEPING:
            (void)dev_tm1640b_onoff_ctrl(false);
            break;
        case TEST_STATE_ERROR:
        default:
            show_error(test_app_error());
            break;
    }
}

void test_display_show_battery(uint8_t percent)
{
    uint8_t buffer[DISPLAY_BYTES] = {0};

    if (percent > 100U) {
        percent = 100U;
    }
    buffer[2] = 0x39U;
    buffer[3] = 0x09U;
    buffer[4] = 0x0FU;
    buffer[5] = 0x30U;
    if (percent == 100U) {
        buffer[6] = digits[1];
        buffer[7] = digits[0];
        buffer[8] = digits[0];
    } else {
        buffer[7] = digits[percent / 10U];
        buffer[8] = digits[percent % 10U];
    }
    send_buffer(buffer);
}

void test_display_shutdown(void)
{
    uint8_t blank[DISPLAY_BYTES];
    memset(blank, 0, sizeof(blank));
    send_buffer(blank);
    dev_tm1640b_sleeping();
}

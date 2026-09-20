#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t last_tx[15];
static uint8_t last_tx_len;
static uint8_t sleeping_call_count;

#include "../Src/scale_digits_display.c"

static void reset_display(void)
{
    memset(buf, 0, sizeof(buf));
    memset(last_tx, 0, sizeof(last_tx));
    last_tx_len = 0;
    sleeping_call_count = 0;
    oz_mode = true;
    _voice_enable = true;
    s_power = 0.0f;
    s_mass = 0.0f;
    s_flowrate = 0.0f;
    s_timing = 0;
    s_auto_mass = 0.0f;
    s_auto_flowrate = 0.0f;
    s_auto_timing = 0;
    display_state = DISP_WEIGHTING;
    ani_cnt = 0;
    ani_state = 0;
    disp_busy_flags = 0x01;
    disp_busy_indication = false;
    disp_peeling_indication = false;
    disp_halt_batlevel = false;
}

static void test_timing_digit_layout_and_indicators(void)
{
    reset_display();
    digits_disp_req(DISP_G_MODE_REQ);
    digits_disp_update_vars(12.3f, 4.5f, 62000U);
    digits_disp_req(DISP_TIMING_REQ);

    digits_disp_processing();

    assert(last_tx_len == 15U);
    assert(last_tx[3] == 0x00U);
    assert(last_tx[4] == (digits_encoding_tab[4] | DIGIT_POINT_ON));
    assert(last_tx[5] == digits_encoding_tab[5]);
    assert(last_tx[8] == digits_encoding_tab[1]);
    assert(last_tx[9] == (digits_encoding_tab[2] | DIGIT_POINT_ON));
    assert(last_tx[10] == digits_encoding_tab[3]);
    assert((last_tx[11] & 0xFEU) == 0xD6U);
    assert((last_tx[12] & 0x04U) != 0U);
}

static void test_auto_state_indicators(void)
{
    reset_display();
    digits_disp_req(DISP_AUTOREADY_REQ);

    digits_disp_processing();

    assert((last_tx[11] & 0xFEU) == 0xAEU);

    digits_disp_req(DISP_AUTOGOING_REQ);

    digits_disp_processing();

    assert((last_tx[11] & 0xFEU) == 0xFEU);

    digits_disp_req(DISP_AUTOEND_REQ);
    digits_disp_processing();
    assert((last_tx[11] & 0xFEU) == 0xAEU);

    for(uint8_t i = 0; i < 8U; i++) {
        digits_disp_processing();
    }
    assert((last_tx[11] & 0xFEU) == 0xFEU);
}


static void test_weighting_indicator(void)
{
    reset_display();
    digits_disp_req(DISP_WEIGHTING_REQ);

    digits_disp_processing();

    assert((last_tx[11] & 0xFEU) == 0x02U);
}


static void test_weight_stops_at_digit_11(void)
{
    reset_display();
    digits_disp_req(DISP_G_MODE_REQ);
    digits_disp_update_vars(123.4f, 0.0f, 62000U);
    digits_disp_req(DISP_WEIGHTING_REQ);
    digits_disp_processing();

    assert(last_tx[3] == digits_encoding_tab[1]);
    assert((last_tx[11] & 0x0EU) == 0x02U);
}

static void test_peeling_request_displays_zero_mass_and_flow(void)
{
    reset_display();
    digits_disp_req(DISP_G_MODE_REQ);
    digits_disp_update_vars(123.4f, 5.6f, 0U);
    digits_disp_req(DISP_TIMING_REQ);
    digits_disp_req(DISP_PEELING_REQ);
    digits_disp_processing();

    assert(last_tx[DIGIT_FLOWRATE_START_IDX + 1] ==
           (digits_encoding_tab[0] | DIGIT_POINT_ON));
    assert(last_tx[DIGIT_FLOWRATE_START_IDX + 2] == digits_encoding_tab[0]);
    assert(last_tx[DIGIT_MASS_START_IDX + 3] ==
           (digits_encoding_tab[0] | DIGIT_POINT_ON));
    assert(last_tx[DIGIT_MASS_START_IDX + 4] == digits_encoding_tab[0]);
}

static void test_unit_mute_and_button_indicators(void)
{
    reset_display();
    digits_disp_req(DISP_G_MODE_REQ);
    digits_disp_req(DISP_VOICE_DISABLE_REQ);
    digits_disp_req(DISP_LEFTPRESS_REQ);
    digits_disp_req(DISP_RIGHTPRESS_REQ);

    digits_disp_processing();

    assert((last_tx[11] & 0x01U) != 0U);
    assert((last_tx[12] & 0x01U) != 0U);
    assert((last_tx[12] & 0x04U) != 0U);
    assert((last_tx[12] & 0x20U) != 0U);
    assert((last_tx[12] & 0x02U) == 0U);

    digits_disp_req(DISP_LEFTRELEASE_REQ);
    digits_disp_req(DISP_RIGHTRELEASE_REQ);
    digits_disp_req(DISP_OZ_MODE_REQ);
    digits_disp_req(DISP_VOICE_ENABLE_REQ);
    digits_disp_processing();

    assert((last_tx[11] & 0x01U) == 0U);
    assert((last_tx[12] & 0x01U) == 0U);
    assert((last_tx[12] & 0x02U) != 0U);
    assert((last_tx[12] & 0x24U) == 0U);
}

static void test_busy_indicators_use_h2_h3_h4(void)
{
    reset_display();

    disp_busyLEDs(0x01U);
    assert((buf[11] & 0x0EU) == 0x02U);
    disp_busyLEDs(0x02U);
    assert((buf[11] & 0x0EU) == 0x04U);
    disp_busyLEDs(0x04U);
    assert((buf[11] & 0x0EU) == 0x08U);

    digits_disp_req(DISP_RIGHTPRESS_REQ);
    digits_disp_req(DISP_INTOBUSY_REQ);
    digits_disp_processing();
    assert((last_tx[12] & 0x20U) != 0U);
}

static void test_shutdown_displays_reference_power_then_halts(void)
{
    reset_display();
    digits_disp_update_power(42U);
    digits_disp_req(DISP_HALT_REQ);

    for(uint8_t i = 0; i < 9U; i++) {
        digits_disp_processing();

        assert(display_state == DISP_BYEBYE);
        assert(last_tx[2] == 0x39U);
        assert(last_tx[3] == 0x09U);
        assert(last_tx[4] == 0x0FU);
        assert(last_tx[5] == 0x30U);
        assert(last_tx[6] == 0x00U);
        assert(last_tx[7] == digits_encoding_tab[4]);
        assert(last_tx[8] == digits_encoding_tab[2]);
        assert(sleeping_call_count == 0U);
    }

    digits_disp_processing();

    assert(display_state == DISP_HALT);
    assert(sleeping_call_count == 1U);
    for(uint8_t i = 0; i < last_tx_len; i++) {
        assert(last_tx[i] == 0x00U);
    }
}

int main(void)
{
    test_timing_digit_layout_and_indicators();
    test_auto_state_indicators();
    test_weighting_indicator();
    test_weight_stops_at_digit_11();
    test_peeling_request_displays_zero_mass_and_flow();
    test_unit_mute_and_button_indicators();
    test_busy_indicators_use_h2_h3_h4();
    test_shutdown_displays_reference_power_then_halts();
    puts("scale_digits_display mapping tests passed");
    return 0;
}

ret_code_t dev_tm1640b_senddata(uint8_t start_addr, uint8_t const *data, uint8_t len)
{
    assert(start_addr == 0U);
    assert(len <= sizeof(last_tx));
    memcpy(last_tx, data, len);
    last_tx_len = len;
    return NS_SUCCESS;
}

ret_code_t dev_tm1640b_onoff_ctrl(bool enabled)
{
    (void)enabled;
    return NS_SUCCESS;
}

void dev_tm1640b_sleeping(void)
{
    sleeping_call_count++;
}
void dev_tm1640b_init(void) {}

ret_code_t dev_tm1640b_wakeup(void)
{
    return NS_SUCCESS;
}

void dev_tm1640b_init_software(void) {}
void dev_tm1640b_wakeup_software(void) {}

void dev_tm1640b_senddata_software(uint8_t start_addr, uint8_t const *data, uint8_t len)
{
    (void)dev_tm1640b_senddata(start_addr, data, len);
}

void dev_tm1640b_onoff_ctrl_software(bool enabled)
{
    (void)enabled;
}
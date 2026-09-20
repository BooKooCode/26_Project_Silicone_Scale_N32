#ifndef INTERNAL_H__
#define INTERNAL_H__

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"

#include "main.h"
#include "app_error.h"
#include "dev_status.h"
#include "scale_mass_mgnt.h"
#include "n32l40x_dbg.h"

typedef SYSTEM_STATE_E system_state_e;
typedef QueueHandle_t xQueueHandle;

typedef enum {
    SYS_OFF = 0U,
    SYS_ON,
} system_on_off_e;

#define SN128_BYTESNUM 16U

typedef struct {
    uint8_t sn128[SN128_BYTESNUM];
} uicr_user_data_s;

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t build;
} ble_dfu_APPversion_s;

typedef struct {
    float voltage;
    float percentage;
    uint8_t level;
    uint8_t reserved[3];
} power_calc_mgnt_result_s;

typedef struct {
    uint8_t raw[32];
} user_config_data_s;

static inline void nrf_uicr_dataget(uicr_user_data_s *uicr_data)
{
    uint8_t uid[UID_LENGTH];
    uint32_t tail_word = 0xA5A55A5AU;

    if (uicr_data == NULL) {
        return;
    }

    GetUID(uid);
    memcpy(uicr_data->sn128, uid, sizeof(uid));
    for (uint32_t index = 0U; index < 3U; index++) {
        uint32_t offset = index * 4U;
        uint32_t uid_word = (uint32_t)uid[offset] |
                            ((uint32_t)uid[offset + 1U] << 8) |
                            ((uint32_t)uid[offset + 2U] << 16) |
                            ((uint32_t)uid[offset + 3U] << 24);
        tail_word ^= uid_word;
    }
    memcpy(&uicr_data->sn128[12], &tail_word, sizeof(tail_word));
}

#endif
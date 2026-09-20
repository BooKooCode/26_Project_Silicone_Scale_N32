#include "test_cal_store.h"
#include "crc.h"
#include "spi.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CAL_MAGIC          0x5443414CU
#define CAL_VERSION        1U
/* Test firmware owns the final two 4 KiB sectors; production KV data there is overwritten. */
#define CAL_SLOT0_ADDRESS  0x001FE000UL
#define CAL_SLOT1_ADDRESS  0x001FF000UL
#define FLASH_TIMEOUT_MS   3000U

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    float calibration;
    uint32_t crc;
} cal_record_t;

static uint32_t active_sequence;
static uint8_t active_slot;

static bool flash_command(const uint8_t *command, size_t command_size,
                          uint8_t *result, size_t result_size)
{
    return spi_flash_write_read(command, command_size, result, result_size) == SPI_FLASH_OK;
}

static bool flash_wait_ready(void)
{
    TickType_t started = xTaskGetTickCount();
    uint8_t command = 0x05U;
    uint8_t status;

    do {
        if (!flash_command(&command, 1U, &status, 1U)) {
            return false;
        }
        if ((status & 0x01U) == 0U) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    } while ((xTaskGetTickCount() - started) < pdMS_TO_TICKS(FLASH_TIMEOUT_MS));
    return false;
}

static bool flash_write_enable(void)
{
    uint8_t command = 0x06U;
    return flash_command(&command, 1U, NULL, 0U);
}

static bool flash_read(uint32_t address, void *data, size_t size)
{
    uint8_t command[4] = {
        0x03U,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };
    return flash_command(command, sizeof(command), data, size);
}

static bool flash_erase_sector(uint32_t address)
{
    uint8_t command[4] = {
        0x20U,
        (uint8_t)(address >> 16),
        (uint8_t)(address >> 8),
        (uint8_t)address
    };
    return flash_write_enable() && flash_command(command, sizeof(command), NULL, 0U) &&
           flash_wait_ready();
}

static bool flash_program(uint32_t address, const void *data, size_t size)
{
    uint8_t command[4 + sizeof(cal_record_t)];

    if (size > sizeof(cal_record_t)) {
        return false;
    }
    command[0] = 0x02U;
    command[1] = (uint8_t)(address >> 16);
    command[2] = (uint8_t)(address >> 8);
    command[3] = (uint8_t)address;
    memcpy(&command[4], data, size);
    return flash_write_enable() && flash_command(command, size + 4U, NULL, 0U) &&
           flash_wait_ready();
}

static bool record_valid(const cal_record_t *record)
{
    uint32_t expected;

    if ((record->magic != CAL_MAGIC) || (record->version != CAL_VERSION) ||
        (record->calibration != record->calibration) ||
        !(record->calibration > -100.0f && record->calibration < 100.0f) ||
        (record->calibration > -0.000001f && record->calibration < 0.000001f)) {
        return false;
    }
    expected = crc32_compute((const uint8_t *)record, offsetof(cal_record_t, crc), NULL);
    return expected == record->crc;
}

bool test_cal_store_load(float *calibration)
{
    cal_record_t slots[2];
    bool valid0;
    bool valid1;

    if (calibration == NULL || !flash_read(CAL_SLOT0_ADDRESS, &slots[0], sizeof(slots[0])) ||
        !flash_read(CAL_SLOT1_ADDRESS, &slots[1], sizeof(slots[1]))) {
        return false;
    }
    valid0 = record_valid(&slots[0]);
    valid1 = record_valid(&slots[1]);
    if (!valid0 && !valid1) {
        active_sequence = 0U;
        active_slot = 1U;
        return false;
    }
    if (valid1 && (!valid0 || (int32_t)(slots[1].sequence - slots[0].sequence) > 0)) {
        active_slot = 1U;
    } else {
        active_slot = 0U;
    }
    active_sequence = slots[active_slot].sequence;
    *calibration = slots[active_slot].calibration;
    return true;
}

bool test_cal_store_save(float calibration)
{
    cal_record_t record = {
        .magic = CAL_MAGIC,
        .version = CAL_VERSION,
        .sequence = active_sequence + 1U,
        .calibration = calibration,
        .crc = 0U
    };
    cal_record_t verify;
    uint8_t target_slot = (uint8_t)(active_slot ^ 1U);
    uint32_t address = target_slot == 0U ? CAL_SLOT0_ADDRESS : CAL_SLOT1_ADDRESS;

    record.crc = crc32_compute((const uint8_t *)&record, offsetof(cal_record_t, crc), NULL);
    if (!flash_erase_sector(address) || !flash_program(address, &record, sizeof(record)) ||
        !flash_read(address, &verify, sizeof(verify)) || !record_valid(&verify) ||
        memcmp(&record, &verify, sizeof(record)) != 0) {
        return false;
    }
    active_slot = target_slot;
    active_sequence = record.sequence;
    return true;
}

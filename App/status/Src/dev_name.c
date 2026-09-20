#include "main.h"
#include "dev_name.h"
#include "n32l40x_dbg.h"
#include "FreeRTOS.h"
#include "task.h"

static uint32_t s_dev_name_seq = 0;


static uint32_t dev_name_mix_entropy(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dUL;
    value ^= value >> 15;
    value *= 0x846ca68bUL;
    value ^= value >> 16;
    return value;
}


uint32_t gen_device_name_rn(void) {
    uint8_t uid[UID_LENGTH];
    uint32_t mixed = 0U;

    GetUID(uid);
    for(uint32_t offset = 0U; offset < UID_LENGTH; offset += 4U) {
        mixed ^= (uint32_t)uid[offset] |
                 ((uint32_t)uid[offset + 1U] << 8) |
                 ((uint32_t)uid[offset + 2U] << 16) |
                 ((uint32_t)uid[offset + 3U] << 24);
    }
    mixed ^= (uint32_t)(((uint64_t)xTaskGetTickCount() * 1000U) / configTICK_RATE_HZ);
    mixed ^= SysTick->VAL;
    mixed ^= (++s_dev_name_seq * 0x9e3779b9UL);

    return dev_name_mix_entropy(mixed) % 1000000UL;
}


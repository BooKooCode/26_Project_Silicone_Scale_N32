#include "crc.h"
#include "main.h"
#include <stddef.h>

static uint32_t crc_nibble_table[16];

void NS_CRC_Init(void)
{
    for(uint32_t index = 0U; index < 16U; index++) {
        uint32_t remainder = index << 28;
        for(uint32_t bit = 0U; bit < 4U; bit++) {
            remainder = (remainder << 1) ^
                        ((remainder & 0x80000000UL) ? 0x04C11DB7UL : 0U);
        }
        crc_nibble_table[index] = remainder;
    }
}

uint32_t crc32_compute(uint8_t const *p_data, uint32_t size, uint32_t const *p_crc)
{
    uint32_t crc = (p_crc == NULL) ? 0xFFFFFFFFU : ~(*p_crc);

    if(p_data == NULL || size == 0U) {
        return ~crc;
    }

    for(uint32_t index = 0U; index < size; index++) {
        crc ^= __RBIT((uint32_t)p_data[index]);
        crc = (crc << 4) ^ crc_nibble_table[crc >> 28];
        crc = (crc << 4) ^ crc_nibble_table[crc >> 28];
    }
    return ~__RBIT(crc);
}
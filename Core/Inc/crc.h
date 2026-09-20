#ifndef CORE_CRC_H
#define CORE_CRC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void NS_CRC_Init(void);
uint32_t crc32_compute(uint8_t const *p_data, uint32_t size, uint32_t const *p_crc);

#ifdef __cplusplus
}
#endif

#endif
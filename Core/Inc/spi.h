#ifndef SPI_H
#define SPI_H

#include <stddef.h>
#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPI_FLASH_OK = 0,
    SPI_FLASH_WRITE_ERROR,
    SPI_FLASH_READ_ERROR
} spi_flash_result_t;

void spi_flash_init(void);
spi_flash_result_t spi_flash_write_read(const uint8_t *write_buf, size_t write_size,
                                       uint8_t *read_buf, size_t read_size);
void NS_SPI1_Init(void);

#ifdef __cplusplus
}
#endif

#endif
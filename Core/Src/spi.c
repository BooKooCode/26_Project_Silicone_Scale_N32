#include "spi.h"
#include <stdbool.h>
#include "FreeRTOS.h"

#define SPI_FLASH_TIMEOUT_MS 1000U

static bool flash_initialized = false;

void NS_SPI1_Init(void)
{
    GPIO_InitType gpio = {0};
    SPI_InitType spi = {0};
    DMA_InitType dma = {0};

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA | RCC_APB2_PERIPH_AFIO |
                           RCC_APB2_PERIPH_SPI1, ENABLE);
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_DMA, ENABLE);
    NVIC_DisableIRQ(DMA_Channel1_IRQn);
    NVIC_DisableIRQ(DMA_Channel2_IRQn);
    DMA_DeInit(DMA_CH1);
    DMA_DeInit(DMA_CH2);
    SPI_I2S_DeInit(SPI1);

    GPIO_ResetBits(SEN_SCK_GPIO_Port, SEN_SCK_Pin);
    gpio.Pin = SEN_SCK_Pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Pull = GPIO_No_Pull;
    gpio.GPIO_Current = GPIO_DC_2mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_Low;
    gpio.GPIO_Alternate = GPIO_AF0_SPI1;
    GPIO_InitPeripheral(SEN_SCK_GPIO_Port, &gpio);
    gpio.Pin = SEN_MISO_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Input;
    GPIO_InitPeripheral(SEN_MISO_GPIO_Port, &gpio);

    spi.DataDirection = SPI_DIR_DOUBLELINE_FULLDUPLEX;
    spi.SpiMode = SPI_MODE_MASTER;
    spi.DataLen = SPI_DATA_SIZE_8BITS;
    spi.CLKPOL = SPI_CLKPOL_LOW;
    spi.CLKPHA = SPI_CLKPHA_SECOND_EDGE;
    spi.NSS = SPI_NSS_SOFT;
    spi.BaudRatePres = SPI_BR_PRESCALER_128;
    spi.FirstBit = SPI_FB_MSB;
    spi.CRCPoly = 7U;
    SPI_Init(SPI1, &spi);
    SPI_SetNssLevel(SPI1, SPI_NSS_HIGH);

    dma.PeriphAddr = (uint32_t)&SPI1->DAT;
    dma.Direction = DMA_DIR_PERIPH_SRC;
    dma.BufSize = 3U;
    dma.PeriphInc = DMA_PERIPH_INC_DISABLE;
    dma.DMA_MemoryInc = DMA_MEM_INC_ENABLE;
    dma.PeriphDataSize = DMA_PERIPH_DATA_SIZE_BYTE;
    dma.MemDataSize = DMA_MemoryDataSize_Byte;
    dma.CircularMode = DMA_MODE_NORMAL;
    dma.Priority = DMA_PRIORITY_HIGH;
    dma.Mem2Mem = DMA_M2M_DISABLE;
    DMA_Init(DMA_CH1, &dma);
    DMA_RequestRemap(DMA_REMAP_SPI1_RX, DMA, DMA_CH1, ENABLE);
    DMA_ConfigInt(DMA_CH1, DMA_INT_TXC | DMA_INT_ERR, ENABLE);
    dma.Direction = DMA_DIR_PERIPH_DST;
    DMA_Init(DMA_CH2, &dma);
    DMA_RequestRemap(DMA_REMAP_SPI1_TX, DMA, DMA_CH2, ENABLE);
    DMA_ConfigInt(DMA_CH2, DMA_INT_ERR, ENABLE);

    NVIC_ClearPendingIRQ(DMA_Channel1_IRQn);
    NVIC_ClearPendingIRQ(DMA_Channel2_IRQn);
    NVIC_SetPriority(DMA_Channel1_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_SetPriority(DMA_Channel2_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(DMA_Channel1_IRQn);
    NVIC_EnableIRQ(DMA_Channel2_IRQn);
    SPI_Enable(SPI1, ENABLE);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void spi_flash_init(void)
{
    GPIO_InitType gpio = {0};
    SPI_InitType spi = {0};

    if (flash_initialized) {
        return;
    }

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB | RCC_APB2_PERIPH_AFIO |
                           RCC_APB2_PERIPH_SPI2, ENABLE);
    GPIO_SetBits(W25QXX_NS_GPIO_Port, W25QXX_NS_Pin);
    gpio.Pin = W25QXX_NS_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio.GPIO_Pull = GPIO_No_Pull;
    gpio.GPIO_Current = GPIO_DC_4mA;
    gpio.GPIO_Slew_Rate = GPIO_Slew_Rate_High;
    GPIO_InitPeripheral(W25QXX_NS_GPIO_Port, &gpio);

    gpio.Pin = W25QXX_SCK_Pin | W25QXX_MOSI_Pin;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Alternate = GPIO_AF0_SPI2;
    GPIO_InitPeripheral(GPIOB, &gpio);
    gpio.Pin = W25QXX_MISO_Pin;
    gpio.GPIO_Mode = GPIO_Mode_Input;
    GPIO_InitPeripheral(GPIOB, &gpio);

    SPI_I2S_DeInit(SPI2);
    spi.DataDirection = SPI_DIR_DOUBLELINE_FULLDUPLEX;
    spi.SpiMode = SPI_MODE_MASTER;
    spi.DataLen = SPI_DATA_SIZE_8BITS;
    spi.CLKPOL = SPI_CLKPOL_LOW;
    spi.CLKPHA = SPI_CLKPHA_FIRST_EDGE;
    spi.NSS = SPI_NSS_SOFT;
    spi.BaudRatePres = SPI_BR_PRESCALER_8;
    spi.FirstBit = SPI_FB_MSB;
    spi.CRCPoly = 7U;
    SPI_Init(SPI2, &spi);
    SPI_SetNssLevel(SPI2, SPI_NSS_HIGH);
    SPI_Enable(SPI2, ENABLE);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    flash_initialized = true;
}

static bool flash_wait_flag(uint16_t flag, FlagStatus expected,
                            uint32_t started, uint32_t timeout_cycles)
{
    for (;;) {
        if ((SPI2->STS & (SPI_MODERR_FLAG | SPI_I2S_OVER_FLAG)) != 0U) {
            return false;
        }
        if ((uint32_t)(DWT->CYCCNT - started) >= timeout_cycles) {
            return false;
        }
        if (SPI_I2S_GetStatus(SPI2, flag) == expected) {
            return true;
        }
    }
}

static bool flash_exchange(const uint8_t *transmit, uint8_t *receive, size_t size)
{
    uint32_t started = DWT->CYCCNT;
    uint32_t timeout_cycles = (SystemCoreClock / 1000U) * SPI_FLASH_TIMEOUT_MS;

    for (size_t offset = 0; offset < size; ++offset) {
        if (!flash_wait_flag(SPI_I2S_TE_FLAG, SET, started, timeout_cycles)) {
            return false;
        }
        SPI_I2S_TransmitData(SPI2, transmit != NULL ? transmit[offset] : 0xFFU);
        if (!flash_wait_flag(SPI_I2S_RNE_FLAG, SET, started, timeout_cycles)) {
            return false;
        }
        uint8_t received = (uint8_t)SPI_I2S_ReceiveData(SPI2);
        if (receive != NULL) {
            receive[offset] = received;
        }
    }
    return flash_wait_flag(SPI_I2S_BUSY_FLAG, RESET, started, timeout_cycles);
}

spi_flash_result_t spi_flash_write_read(const uint8_t *write_buf, size_t write_size,
                                       uint8_t *read_buf, size_t read_size)
{
    spi_flash_result_t result = SPI_FLASH_OK;

    if (write_size != 0U && write_buf == NULL) {
        return SPI_FLASH_WRITE_ERROR;
    }
    if (read_size != 0U && read_buf == NULL) {
        return SPI_FLASH_READ_ERROR;
    }
    if (write_size == 0U && read_size == 0U) {
        return SPI_FLASH_OK;
    }

    spi_flash_init();
    GPIO_ResetBits(W25QXX_NS_GPIO_Port, W25QXX_NS_Pin);
    if (write_size != 0U && !flash_exchange(write_buf, NULL, write_size)) {
        result = SPI_FLASH_WRITE_ERROR;
    } else if (read_size != 0U && !flash_exchange(NULL, read_buf, read_size)) {
        result = SPI_FLASH_READ_ERROR;
    }
    GPIO_SetBits(W25QXX_NS_GPIO_Port, W25QXX_NS_Pin);

    if (result != SPI_FLASH_OK) {
        SPI_Enable(SPI2, DISABLE);
        flash_initialized = false;
    }
    return result;
}
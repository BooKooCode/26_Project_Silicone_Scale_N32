# SFUD库使用注意事项

## 一、原始库的改动
对于低功耗设备（例如BOOKOO EM），即使SPI FLASH由于某种原因掉线，也不会有影响，故需要适当修改代码：

把发送失败的重试次数改短（`sfud_port.h`）：
```c
/* Timeout: (retry.times) * 1ms */
flash->retry.times = 200;
```

把`sfud.h`的`reset()`函数进行修改：
```c
sfud_err result = SFUD_SUCCESS;
const sfud_spi *spi = &flash->spi;
uint8_t cmd_data[2];

SFUD_ASSERT(flash);

// it is recommended to check the BUSY bit and the SUS bit in Status Register 
// before issuing the Reset command sequence.
// while(wait_busy(flash) != SFUD_SUCCESS){}
result = wait_busy(flash);
if(result != SFUD_SUCCESS)	return result;
```

## 二、使用事项

该库目前暂不支持在资源宏`resoure_occuptation.h`中配置多个实体SPI FLASH（即仅支持单个FLASH）；若需修改FLASH为其他型号，需在`sfud_cfg.h`中进行修改。
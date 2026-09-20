#ifndef __EXT_STORE_CONF_H__
#define __EXT_STORE_CONF_H__

#define ONCE_ERASE_BLOCK_MAX_SIZE           0x00010000

#define PARAM_AREA_START_ADDR_IN_FLASH      0x00000000
#define PARAM_AREA_CAPACITY                 0x00200000
#define PARAM_AREA_END_ADDR_IN_FLASH        (PARAM_AREA_START_ADDR_IN_FLASH + PARAM_AREA_CAPACITY)

#define HASH_TABLE_SIZE                     251U
#define STORE_KEY_MAX_LEN                   24U
#define STORE_STRING_MAX_LEN                64U

#define CHECK_HASH_KEY                      0

#endif


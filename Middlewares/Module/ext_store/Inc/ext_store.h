#ifndef __EXT_FLASH_H__
#define __EXT_FLASH_H__

#include <stdint.h>
#include "util.h"
#include "bookoo_error_def.h"
#include "ext_store_conf.h"

#define SAFE_START_VERIFI                       0x4CA09F8742001EFC
#define SAFE_END_VERIFI                         0x21DE3AEDC987D18D


#define SECTOR_ACTIVE                           0xA5A5
#define SECTOR_BACKUP                           0xA0A0
#define SECTOR_INVALID                          0x0000


#define PARAM_NOT_FOUND                         0xFF
#define PARAM_INIT                              0xAA
#define PARAM_CORRECT                           0xA5
#define PARAM_MODIFIED                          0xA0
#define PARAM_DELETE                            0x00


#pragma pack(1)
typedef struct {
    uint64_t uid_start;
    uint32_t write_count;
    uint16_t sector_status;
    uint16_t sector_index;
    uint16_t sector_count;
    uint32_t param_size;
} ext_store_packet_start_t;
#pragma pack()


#pragma pack(1)
typedef struct {
    uint32_t crc32_backup;
    uint32_t crc32_active;
    uint64_t uid_end;
} ext_store_packet_end_t;
#pragma pack()


#define EXT_PACKET_STRUCT_SIZE                  (sizeof(ext_store_packet_start_t) + sizeof(ext_store_packet_end_t))
#define EXT_PACKET_SECTOR_BASE_SIZE             (0x1000)
#define EXT_PACKET_PARAM_MINI_SIZE              (EXT_PACKET_SECTOR_BASE_SIZE - EXT_PACKET_STRUCT_SIZE)
#define GET_STORE_SECTOR_COUNT(param_size)      { \
                                                    uint32_t sector_count = param_size / EXT_PACKET_PARAM_MINI_SIZE; \
                                                    if(param_size % EXT_PACKET_PARAM_MINI_SIZE) { \
                                                        sector_count += 1; \
                                                    } \
                                                    return sector_count; \
                                                }


#define ADDR_SAFE_UID_START                     (0x00000000)
#define ADDR_WRITE_COUNT                        (ADDR_SAFE_UID_START + sizeof(uint64_t))
#define ADDR_SECTOR_STATUS                      (ADDR_WRITE_COUNT + sizeof(uint32_t))
#define ADDR_SECTOR_INDEX                       (ADDR_SECTOR_STATUS + sizeof(uint16_t))
#define ADDR_SECTOR_COUNT                       (ADDR_SECTOR_INDEX + sizeof(uint16_t))
#define ADDR_PARAM_SIZE                         (ADDR_SECTOR_COUNT + sizeof(uint16_t))
#define ADDR_BASIC_PARAM                        (ADDR_PARAM_SIZE + sizeof(uint32_t))

#define ADDR_SAFE_UID_END                       (EXT_PACKET_SECTOR_BASE_SIZE - sizeof(uint64_t))
#define ADDR_SECTOR_CRC32_ACTIVE                (ADDR_SAFE_UID_END - sizeof(uint32_t))
#define ADDR_SECTOR_CRC32_BACKUP                (ADDR_SECTOR_CRC32_ACTIVE - sizeof(uint32_t))


typedef uint32_t (*write_ext_flash_h)(uint32_t addr, uint32_t size, uint8_t *data);
typedef uint32_t (*read_ext_flash_h)(uint32_t addr, uint32_t size, uint8_t *dist);
typedef uint32_t (*erase_ext_flash_h)(uint32_t addr, uint32_t size);


typedef struct {
    write_ext_flash_h write;
    read_ext_flash_h read;
    erase_ext_flash_h erase;
} ext_flash_handler_t;


typedef struct {
    const char *key;
    FORMAT_4BYTES_U cache;
    FORMAT_4BYTES_U default_val;
    FORMAT_4BYTES_U lo_limit;
    FORMAT_4BYTES_U hi_limit;
    uint8_t type;
} numeric_param_t;


typedef struct {
    const char *key;
    char *default_val;
    uint16_t val_max_len;
} string_param_t;


uint32_t get_ext_flash_write_count(void);

SYSTEM_ERROR_CODE_E erase_all_params_sector(void);

SYSTEM_ERROR_CODE_E load_active_store_from_ext_flash(void);

bool is_active_or_backup_missing(void);

SYSTEM_ERROR_CODE_E create_new_active_store(void);

SYSTEM_ERROR_CODE_E ext_store_init(ext_flash_handler_t *flash_handler);

SYSTEM_ERROR_CODE_E kv_get_type(const char *key, uint8_t *value_type);

SYSTEM_ERROR_CODE_E kv_get_len(const char *key, uint16_t *value_len);

SYSTEM_ERROR_CODE_E kv_get(const char *key, uint8_t *value_buf, uint16_t value_len);

SYSTEM_ERROR_CODE_E kv_set(const char *key, uint8_t type, uint8_t *value_buf, uint16_t value_len);

SYSTEM_ERROR_CODE_E kv_delete(const char *key);

SYSTEM_ERROR_CODE_E kv_link_cfg_info(const char *key, void *cfg_info);

SYSTEM_ERROR_CODE_E kv_get_cfg_info(const char *key, void **dist_cfg_info);

SYSTEM_ERROR_CODE_E kv_store_init(void);


#endif


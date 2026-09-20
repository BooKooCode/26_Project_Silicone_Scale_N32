#ifndef __DEV_CONFIG_H__
#define __DEV_CONFIG_H__

#include "default_parameters.h"
#include "bookoo_error_def.h"
#include "ext_store_conf.h"


#define NEED_WRITE_CONFIG_BIT           (1 << 0)
#define NEED_RESTORE_CONFIG_BIT         (1 << 1)
#define NEED_ERASE_ALL_CONFIG_BIT       (1 << 2)
#define CONFIG_STORE_WAIT_TIMEOUT_MS    5000U


SYSTEM_ERROR_CODE_E get_kv_item_type(const char *key, uint8_t *type);

SYSTEM_ERROR_CODE_E get_kv_item_len(const char *key, uint16_t *len);

SYSTEM_ERROR_CODE_E get_kv_param_value(const char *key, void *value);

SYSTEM_ERROR_CODE_E set_kv_param_value(const char *key, void *value);

SYSTEM_ERROR_CODE_E get_kv_str_value(const char *key, char *value, uint16_t len);

SYSTEM_ERROR_CODE_E set_kv_str_value(const char *key, char *value, uint16_t len);

SYSTEM_ERROR_CODE_E delete_kv_item(const char *key);

SYSTEM_ERROR_CODE_E get_uicr_sn128(uint8_t *sn128);

SYSTEM_ERROR_CODE_E need_restore_factory(void);

SYSTEM_ERROR_CODE_E need_erase_all(void);

SYSTEM_ERROR_CODE_E need_write_config(void);

SYSTEM_ERROR_CODE_E wait_config_store(void);

void dev_store_cfg_init(void);

#endif

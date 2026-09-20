#include <stdlib.h>
#include <string.h>
#include "crc.h"
#include "hash64.h"
#include "double_list.h"
#include "ext_store.h"
#include "default_parameters.h"
#include "log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define EXT_STORE_LOG_ERROR(...) do { (void)LOG_ERROR("ext_store", __VA_ARGS__); } while (0)
#define EXT_STORE_LOG_WARNING(...) do { (void)LOG_WARNING("ext_store", __VA_ARGS__); } while (0)
#define EXT_STORE_LOG_INFO(...) do { (void)LOG_INFO("ext_store", __VA_ARGS__); } while (0)


#ifndef USE_RTOS_MEMORY
#define USE_RTOS_MEMORY         0
#endif

#ifndef CHECK_HASH_KEY
#define CHECK_HASH_KEY          1
#endif


#pragma pack(1)
typedef struct {
    uint64_t hash;
    uint8_t state;
    uint8_t type;
    uint16_t key_len;
    uint16_t value_len;
} kv_header_t;
#pragma pack()


typedef struct hash_table_entry {
    uint32_t rela_addr;
    uint32_t cfg_info;
    void *m_temp;
    kv_header_t header;
    struct hash_table_entry *next;
} hash_entry_t;


typedef struct {
    bool is_init;
    ext_flash_handler_t hw_handler;
    /* current active sectors info */
    uint32_t start_addr;
    uint32_t param_end_addr;
    uint32_t total_param_size;
    uint16_t sector_count;
    /* current backup sectors info */
    uint32_t backup_start_addr;
    uint16_t backup_sector_count;
    /* next active sectors info */
    uint32_t next_total_param_size;
    uint32_t write_count;
    uint32_t fault_offset;
    list_t new_list;
    bool find_active;
    bool find_backup;
    /* Os mutex */
    SemaphoreHandle_t kv_mutex;
} ext_store_mgnt_t;


typedef struct {
    kv_header_t header;
    char *key;
    uint8_t *value;
} hash_cache_t;


ext_store_mgnt_t s_ext_store_mgnt = {NULL};

/* Hash Table in RAM */
static hash_entry_t *hash_table[HASH_TABLE_SIZE] = {NULL};


static bool is_packet_start_valid(const ext_store_packet_start_t *packet_start) {
    uint32_t max_sector_count = PARAM_AREA_CAPACITY / EXT_PACKET_SECTOR_BASE_SIZE;
    return packet_start->sector_count > 0 &&
           packet_start->sector_count <= max_sector_count &&
           packet_start->sector_index < packet_start->sector_count &&
           packet_start->param_size <= EXT_PACKET_PARAM_MINI_SIZE;
}


static void* s_calloc(size_t num, size_t size) {
#if USE_RTOS_MEMORY
    void *addr = pvPortMalloc(num * size);
    memset(addr, 0, num * size);
    return addr;
#else
    return calloc(num, size);
#endif
}


static void s_free(void *addr) {
#if USE_RTOS_MEMORY
    vPortFree(addr);
#else
    free(addr);
#endif
}


static uint32_t cal_param_sector_crc32(uint32_t addr, uint32_t size) {
    uint32_t _cur_addr = addr;
    uint32_t _rem_size = size;
    uint32_t _crc32 = 0;
    uint8_t _data[128] = {0};
    uint16_t _read_cnt = (size % 128) ? ((size >> 7) + 1) : (size >> 7);
    for(uint16_t i = 0; i < _read_cnt; i++) {
        _cur_addr = addr + (i << 7);
        uint32_t __read_size = (_rem_size >= 128) ? 128 : _rem_size;
        memset(_data, 0, 128);
        s_ext_store_mgnt.hw_handler.read(_cur_addr, __read_size, _data);
        _crc32 = crc32_compute(_data, __read_size, &_crc32);
        _rem_size = _rem_size - __read_size;
    }
    return _crc32;
}


static uint32_t move_param_addr_cross_sector(uint32_t _addr, uint32_t _move_len) {
    uint32_t _addr1;
    if(((_addr % EXT_PACKET_SECTOR_BASE_SIZE) + _move_len) < ADDR_SECTOR_CRC32_BACKUP) {
        _addr1 = _addr + _move_len;
    } else {
        uint32_t _res_len = _move_len - (ADDR_SECTOR_CRC32_BACKUP - (_addr % EXT_PACKET_SECTOR_BASE_SIZE));
        _addr1 = _addr + (EXT_PACKET_SECTOR_BASE_SIZE - (_addr % EXT_PACKET_SECTOR_BASE_SIZE)) + ADDR_BASIC_PARAM + _res_len;
    }
    return _addr1;
}


static hash_entry_t *get_hash_entry(kv_header_t *_kv) {
    if(NULL == _kv) {
        return NULL;
    }
    int16_t _hash_index = _kv->hash % HASH_TABLE_SIZE;
    hash_entry_t *_current = hash_table[_hash_index];
    while(_current != NULL) {
        if(_current->header.hash != _kv->hash || _current->header.key_len != _kv->key_len) {
            _current = _current->next;
            continue;
        }
        return _current;
    }
    return NULL;
}


static void change_item_info_in_hash_table(kv_header_t *_new_kv, uint8_t *_new_state, uint32_t *_new_rela_addr) {
    if(NULL == _new_kv) {
        return;
    }
    if(NULL == _new_state && NULL == _new_rela_addr) {
        return;
    }
    uint16_t _hash_index = _new_kv->hash % HASH_TABLE_SIZE;
    hash_entry_t *_current = hash_table[_hash_index];
    while(_current != NULL) {
        if(_current->header.hash != _new_kv->hash || _current->header.key_len != _new_kv->key_len) {
            _current = _current->next;
            continue;
        }
        if(_new_rela_addr) {
            _current->rela_addr = *_new_rela_addr;
        }
        if(_new_state) {
            _current->header.state = *_new_state;
        }
        break;
    }
}


static hash_entry_t *get_kv_in_hash_table(uint64_t hash) {
    if(0 == hash) {
        return NULL;
    }
    uint16_t _hash_index = hash % HASH_TABLE_SIZE;
    hash_entry_t *_current = hash_table[_hash_index];
    while(_current != NULL) {
        if(_current->header.hash == hash) {
            return _current;
        }
        _current = _current->next;
    }
    return NULL;
}


static SYSTEM_ERROR_CODE_E write_params_to_sector(uint32_t addr, uint8_t *buf, uint32_t len) {
    if(false == s_ext_store_mgnt.is_init) {
        return ERR_NOT_INIT;
    }
    if(addr > PARAM_AREA_CAPACITY) {
        return ERR_OUT_OF_RANGE;
    }
    if(NULL == buf) {
        return ERR_INVALID_POINTER;
    }
    if(len >= EXT_PACKET_PARAM_MINI_SIZE) {
        return ERR_INVALID_LEN;
    }
    if(((addr % EXT_PACKET_SECTOR_BASE_SIZE) + len) < ADDR_SECTOR_CRC32_BACKUP) {
        /* Write directly */
        if(ERR_NONE != s_ext_store_mgnt.hw_handler.write(addr, len, buf)) {
            return ERR_FLASH_WRITE_FAIL;
        }
    } else {
        /* Segmented writing */
        uint32_t _write_comp_len = ADDR_SECTOR_CRC32_BACKUP - (addr % EXT_PACKET_SECTOR_BASE_SIZE);
        if(ERR_NONE != s_ext_store_mgnt.hw_handler.write(addr, _write_comp_len, buf)) {
            return ERR_FLASH_WRITE_FAIL;
        }
        uint32_t _res_len = len - (ADDR_SECTOR_CRC32_BACKUP - (addr % EXT_PACKET_SECTOR_BASE_SIZE));
        uint32_t _next_begin_addr = addr + (EXT_PACKET_SECTOR_BASE_SIZE - (addr % EXT_PACKET_SECTOR_BASE_SIZE)) + ADDR_BASIC_PARAM;
        if(ERR_NONE != s_ext_store_mgnt.hw_handler.write(_next_begin_addr, _res_len, buf + _write_comp_len)) {
            return ERR_FLASH_WRITE_FAIL;
        }
    }
    return ERR_NONE;
}


static SYSTEM_ERROR_CODE_E write_params_to_active_sector(uint32_t rela_addr, uint8_t *buf, uint32_t len) {
    return write_params_to_sector(rela_addr + s_ext_store_mgnt.start_addr, buf, len);
}


static SYSTEM_ERROR_CODE_E read_params_from_sector(uint32_t addr, uint8_t *buf, uint32_t len) {
    if(false == s_ext_store_mgnt.is_init) {
        return ERR_NOT_INIT;
    }
    if(addr > PARAM_AREA_CAPACITY) {
        return ERR_OUT_OF_RANGE;
    }
    if(NULL == buf) {
        return ERR_INVALID_POINTER;
    }
    if(len >= EXT_PACKET_PARAM_MINI_SIZE) {
        return ERR_INVALID_LEN;
    }
    if(((addr % EXT_PACKET_SECTOR_BASE_SIZE) + len) < ADDR_SECTOR_CRC32_BACKUP) {
        /* Read directly */
        if(ERR_NONE != s_ext_store_mgnt.hw_handler.read(addr, len, buf)) {
            return ERR_FLASH_READ_FAIL;
        }
    } else {
        /* Segmented reading */
        uint32_t _read_comp_len = ADDR_SECTOR_CRC32_BACKUP - (addr % EXT_PACKET_SECTOR_BASE_SIZE);
        if(ERR_NONE != s_ext_store_mgnt.hw_handler.read(addr, _read_comp_len, buf)) {
            return ERR_FLASH_READ_FAIL;
        }
        uint32_t _res_len = len - (ADDR_SECTOR_CRC32_BACKUP - (addr % EXT_PACKET_SECTOR_BASE_SIZE));
        uint32_t _next_begin_addr = addr + (EXT_PACKET_SECTOR_BASE_SIZE - (addr % EXT_PACKET_SECTOR_BASE_SIZE)) + ADDR_BASIC_PARAM;
        if(ERR_NONE != s_ext_store_mgnt.hw_handler.read(_next_begin_addr, _res_len, buf + _read_comp_len)) {
            return ERR_FLASH_READ_FAIL;
        }
    }
    return ERR_NONE;
}


static SYSTEM_ERROR_CODE_E read_params_from_active_sector(uint32_t rela_addr, uint8_t *buf, uint32_t len) {
    return read_params_from_sector(rela_addr + s_ext_store_mgnt.start_addr, buf, len);
}


static SYSTEM_ERROR_CODE_E verify_param_write_result(uint32_t write_addr, uint8_t *src_data, uint8_t *read_data, uint32_t len) {
    if(false == s_ext_store_mgnt.is_init) {
        return ERR_NOT_INIT;
    }
    if(write_addr > PARAM_AREA_CAPACITY) {
        return ERR_OUT_OF_RANGE;
    }
    if(NULL == src_data || NULL == read_data) {
        return ERR_INVALID_POINTER;
    }
    if(len >= EXT_PACKET_PARAM_MINI_SIZE) {
        return ERR_INVALID_LEN;
    }
    if(ERR_NONE != read_params_from_sector(write_addr, read_data, len)) {
        return ERR_FLASH_READ_FAIL;
    }
    if(0 != memcmp(src_data, read_data, len)) {
        EXT_STORE_LOG_ERROR("Verify write result fail, address: 0x%08x", write_addr);
        return ERR_FLASH_WRITE_FAIL;
    }
    return ERR_NONE;
}


static SYSTEM_ERROR_CODE_E verify_directly_write_result(uint32_t write_addr, uint8_t *src_data, uint8_t *read_data, uint32_t len) {
    if(false == s_ext_store_mgnt.is_init) {
        return ERR_NOT_INIT;
    }
    if(write_addr > PARAM_AREA_CAPACITY) {
        return ERR_OUT_OF_RANGE;
    }
    if(NULL == src_data || NULL == read_data) {
        return ERR_INVALID_POINTER;
    }
    if(len >= EXT_PACKET_PARAM_MINI_SIZE) {
        return ERR_INVALID_LEN;
    }
    s_ext_store_mgnt.hw_handler.read(write_addr, len, read_data);
    if(0 != memcmp(src_data, read_data, len)) {
        EXT_STORE_LOG_ERROR("Write verify fail, address: 0x%08x", write_addr);
        return ERR_FLASH_WRITE_FAIL;
    }
    return ERR_NONE;
}


static SYSTEM_ERROR_CODE_E erase_params_sector(uint32_t addr, uint32_t len) {
    if((addr + len) > (PARAM_AREA_START_ADDR_IN_FLASH + PARAM_AREA_CAPACITY)) {
        return ERR_INVALID_LEN;
    }
    uint32_t _erase_len = len;
    uint32_t _start_addr = addr;
    while(_erase_len) {
        uint32_t _cur_erase_len = (_erase_len <= ONCE_ERASE_BLOCK_MAX_SIZE) ? _erase_len : ONCE_ERASE_BLOCK_MAX_SIZE;
        int _res = s_ext_store_mgnt.hw_handler.erase(_start_addr, _cur_erase_len);
        if(_res != ERR_NONE) {
            EXT_STORE_LOG_ERROR("[erase] fail, start addr: 0x%08x, erase size: 0x%08x", _start_addr, _cur_erase_len);
            return ERR_FLASH_ERASE_FAIL;
        }
        EXT_STORE_LOG_INFO("[erase] success, start addr: 0x%08x, erase size: 0x%08x", _start_addr, _cur_erase_len);
        _start_addr += _cur_erase_len;
        _erase_len -= _cur_erase_len;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    
    return ERR_NONE;
}


uint32_t get_ext_flash_write_count(void) {
    return s_ext_store_mgnt.write_count;
}


SYSTEM_ERROR_CODE_E erase_all_params_sector(void) {
    if(false == s_ext_store_mgnt.is_init) {
        return ERR_NOT_INIT;
    }
    return erase_params_sector(PARAM_AREA_START_ADDR_IN_FLASH, PARAM_AREA_CAPACITY);
}


SYSTEM_ERROR_CODE_E load_active_store_from_ext_flash(void) {
    if(false == s_ext_store_mgnt.is_init) {
        return ERR_NOT_INIT;
    }
    uint32_t _end = PARAM_AREA_END_ADDR_IN_FLASH;
    /* Ensure _end is an integer multiple of the capacity */
    if(_end % EXT_PACKET_SECTOR_BASE_SIZE) {
        _end -= (_end % EXT_PACKET_SECTOR_BASE_SIZE);
    }
    uint32_t _addr = _end;
    bool _crc_pass = false;
    uint32_t _active_begin_addr = 0;
    uint32_t _total_param_size = 0;
    uint32_t _last_param_size = 0;
    uint32_t _selected_total_param_size = 0;
    uint32_t _selected_last_param_size = 0;
    ext_store_packet_start_t _packet_start = {0};
    ext_store_packet_start_t _selected_packet_start = {0};
    ext_store_packet_end_t _packet_end;
    
    uint8_t _zero_buf[EXT_PACKET_STRUCT_SIZE] = {0};
    /* Find latest param frist sector */
    while(_addr > PARAM_AREA_START_ADDR_IN_FLASH) {
        if(_addr >= EXT_PACKET_SECTOR_BASE_SIZE) {
            _addr -= EXT_PACKET_SECTOR_BASE_SIZE;
        } else {
            _addr = PARAM_AREA_START_ADDR_IN_FLASH;
        }
        /* 1. Find the correct package structure */
        s_ext_store_mgnt.hw_handler.read(_addr + ADDR_SAFE_UID_START, sizeof(ext_store_packet_start_t), (uint8_t *)&_packet_start);
        s_ext_store_mgnt.hw_handler.read(_addr + ADDR_SECTOR_CRC32_BACKUP, sizeof(ext_store_packet_end_t), (uint8_t *)&_packet_end);
        if(_packet_start.uid_start != SAFE_START_VERIFI || _packet_end.uid_end != SAFE_END_VERIFI) {
            _total_param_size = 0;
            continue;
        }
        /* 2. Check sector status */
        if(_packet_start.sector_status != SECTOR_ACTIVE && _packet_start.sector_status != SECTOR_BACKUP) {
            _total_param_size = 0;
            continue;
        }
        if(false == is_packet_start_valid(&_packet_start)) {
            _total_param_size = 0;
            continue;
        }
        /* 3. CRC check */
        uint32_t _cur_crc32 = cal_param_sector_crc32(_addr + ADDR_SAFE_UID_START, sizeof(ext_store_packet_start_t) + _packet_start.param_size);
        if(_cur_crc32 != _packet_end.crc32_active && _cur_crc32 != _packet_end.crc32_backup) {
            _crc_pass = false;
            _total_param_size = 0;
            continue;
        }
        /* 4. Find the first package in the sub package */
        /* Reset the flag of crc pass on last parameters sector */
        if(_packet_start.sector_index == (_packet_start.sector_count - 1)) {
            _crc_pass = true;
            _total_param_size = 0;
            _last_param_size = _packet_start.param_size;
        }
        _total_param_size += _packet_start.param_size;
        /* 5. Ensure that decode first sector */
        if(0 != _packet_start.sector_index) {
            continue;
        }
        if(false == _crc_pass) {
            continue;
        }
        /* 6. Record backup sector */
        if(_packet_start.sector_status == SECTOR_BACKUP) {
            s_ext_store_mgnt.backup_start_addr = _addr;
            s_ext_store_mgnt.backup_sector_count = _packet_start.sector_count;
            s_ext_store_mgnt.find_backup = true;
            _selected_packet_start = _packet_start;
            _selected_total_param_size = _total_param_size;
            _selected_last_param_size = _last_param_size;
            continue;
        }
        /* 6. Get the active address */
        _active_begin_addr = _addr;
        s_ext_store_mgnt.find_active = true;
        _selected_packet_start = _packet_start;
        _selected_total_param_size = _total_param_size;
        _selected_last_param_size = _last_param_size;
        break;
    }
    if(false == s_ext_store_mgnt.find_active && false == s_ext_store_mgnt.find_backup) {
        return ERR_NOT_EXIST;
    }
    else if(false == s_ext_store_mgnt.find_active && s_ext_store_mgnt.find_backup) {
        /* Only backup: Use backup sectors as active sectors */
        _active_begin_addr = s_ext_store_mgnt.backup_start_addr;
    }
    /* Try to find the backup as much as possible */
    if(false == s_ext_store_mgnt.find_backup && _active_begin_addr >= EXT_PACKET_SECTOR_BASE_SIZE) {
        _addr = _active_begin_addr;
        ext_store_packet_start_t _backup_packet_start;
        ext_store_packet_end_t _backup_packet_end;
        do {
            if(_addr >= EXT_PACKET_SECTOR_BASE_SIZE) {
                _addr -= EXT_PACKET_SECTOR_BASE_SIZE;
            } else {
                _addr = PARAM_AREA_START_ADDR_IN_FLASH;
            }
            s_ext_store_mgnt.hw_handler.read(_addr + ADDR_SAFE_UID_START, sizeof(ext_store_packet_start_t), (uint8_t *)&_backup_packet_start);
            s_ext_store_mgnt.hw_handler.read(_addr + ADDR_SECTOR_CRC32_BACKUP, sizeof(ext_store_packet_end_t), (uint8_t *)&_backup_packet_end);
            if(_backup_packet_start.uid_start != SAFE_START_VERIFI || _backup_packet_end.uid_end != SAFE_END_VERIFI) {
                continue;
            }
            if(_backup_packet_start.sector_status != SECTOR_BACKUP) {
                continue;
            }
            if(false == is_packet_start_valid(&_backup_packet_start)) {
                continue;
            }
            uint32_t _cur_crc32 = cal_param_sector_crc32(_addr + ADDR_SAFE_UID_START, sizeof(ext_store_packet_start_t) + _backup_packet_start.param_size);
            if(_cur_crc32 != _backup_packet_end.crc32_backup) {
                _crc_pass = false;
                continue;
            }
            if(_backup_packet_start.sector_index == (_backup_packet_start.sector_count - 1)) {
                _crc_pass = true;
            }
            if(0 != _backup_packet_start.sector_index) {
                continue;
            }
            if(false == _crc_pass) {
                continue;
            }
            s_ext_store_mgnt.backup_start_addr = _addr;
            s_ext_store_mgnt.backup_sector_count = _backup_packet_start.sector_count;
            s_ext_store_mgnt.find_backup = true;
            break;
        } while(_addr > PARAM_AREA_START_ADDR_IN_FLASH);
    }

    if(false == is_packet_start_valid(&_selected_packet_start)) {
        return ERR_INVALID_LEN;
    }
    s_ext_store_mgnt.start_addr = _active_begin_addr;
    s_ext_store_mgnt.total_param_size = _selected_total_param_size;
    s_ext_store_mgnt.next_total_param_size = _selected_total_param_size;
    s_ext_store_mgnt.sector_count = _selected_packet_start.sector_count;
    s_ext_store_mgnt.param_end_addr = EXT_PACKET_SECTOR_BASE_SIZE * (_selected_packet_start.sector_count - 1) + \
                                        sizeof(ext_store_packet_start_t) + _selected_last_param_size;
    s_ext_store_mgnt.write_count = _selected_packet_start.write_count;
    return ERR_NONE;
}


bool is_active_or_backup_missing(void) {
    return (false == s_ext_store_mgnt.find_active) || (false == s_ext_store_mgnt.find_backup);
}


SYSTEM_ERROR_CODE_E create_new_active_store(void) {
    if(false == s_ext_store_mgnt.is_init) {
        return ERR_NOT_INIT;
    }
    /* Find next empty sector */
    uint32_t _w_begin = s_ext_store_mgnt.start_addr + (s_ext_store_mgnt.sector_count + s_ext_store_mgnt.fault_offset) * EXT_PACKET_SECTOR_BASE_SIZE;
    uint16_t _next_sector_count = s_ext_store_mgnt.next_total_param_size / EXT_PACKET_PARAM_MINI_SIZE;
    if(s_ext_store_mgnt.next_total_param_size % EXT_PACKET_PARAM_MINI_SIZE) {
        _next_sector_count += 1;
    }
    /* Must be limited before Last integral sector */
    if(_w_begin > (PARAM_AREA_CAPACITY - EXT_PACKET_SECTOR_BASE_SIZE) || \
        ((_w_begin + _next_sector_count * EXT_PACKET_SECTOR_BASE_SIZE) > PARAM_AREA_CAPACITY)) {
        _w_begin = PARAM_AREA_START_ADDR_IN_FLASH;
    }
    uint32_t _w_addr = _w_begin;
    /* Prepare empty data area */
    uint32_t _erase_size = _next_sector_count * EXT_PACKET_SECTOR_BASE_SIZE;
    int _erase_res = s_ext_store_mgnt.hw_handler.erase(_w_begin, _erase_size);
    if(_erase_res != ERR_NONE) {
        EXT_STORE_LOG_ERROR("Write erase fail, start addr: 0x%08x, erase size: 0x%08x", _w_begin, _erase_size);
        return ERR_FLASH_ERASE_FAIL;
    }
    /* Params copy */
    uint32_t _src_addr = s_ext_store_mgnt.start_addr + ADDR_BASIC_PARAM;
    uint32_t _src_addr_end = s_ext_store_mgnt.start_addr + s_ext_store_mgnt.param_end_addr;
    _w_addr += ADDR_BASIC_PARAM;
    
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    /* Traverse the old active sectors */
    while(_src_addr < _src_addr_end) {
        kv_header_t _kv_header = {0};
        read_params_from_sector(_src_addr, (uint8_t *)&_kv_header, sizeof(kv_header_t));
        uint32_t _data_len = _kv_header.key_len + _kv_header.value_len;
        uint32_t _package_len = sizeof(kv_header_t) + _data_len;
        if(_kv_header.state == PARAM_CORRECT) {
            /* Copy in order */
            uint8_t *_copy_data = s_calloc(2, _package_len);
            if(NULL == _copy_data) {
                goto NO_MEMORY;
            }
            memcpy(_copy_data, &_kv_header, sizeof(kv_header_t));
            SYSTEM_ERROR_CODE_E __ret = read_params_from_sector(_src_addr + sizeof(kv_header_t), &_copy_data[sizeof(kv_header_t)], _data_len);
            if(__ret != ERR_NONE) {
                s_free(_copy_data);
                goto READ_FAIL;
            }
            __ret = write_params_to_sector(_w_addr, _copy_data, _package_len);
            if(__ret != ERR_NONE) {
                s_ext_store_mgnt.fault_offset = (_w_addr - _w_begin) / EXT_PACKET_SECTOR_BASE_SIZE;
                if((_w_addr - _w_begin) % EXT_PACKET_SECTOR_BASE_SIZE) {
                    s_ext_store_mgnt.fault_offset += 1;
                }
                s_free(_copy_data);
                goto WRITE_FAIL;
            }
            __ret = verify_param_write_result(_w_addr, _copy_data, &_copy_data[_package_len], _package_len);
            if(__ret != ERR_NONE) {
                s_ext_store_mgnt.fault_offset = (_w_addr - _w_begin) / EXT_PACKET_SECTOR_BASE_SIZE;
                if((_w_addr - _w_begin) % EXT_PACKET_SECTOR_BASE_SIZE) {
                    s_ext_store_mgnt.fault_offset += 1;
                }
                s_free(_copy_data);
                goto WRITE_FAIL;
            }
            s_free(_copy_data);
            /* Update flash address in the hash table */
            uint32_t _new_rela_addr = _w_addr - _w_begin;
            change_item_info_in_hash_table(&_kv_header, &_kv_header.state, &_new_rela_addr);
            /* Update next write address */
            _w_addr = move_param_addr_cross_sector(_w_addr, _package_len);
        } 
        else if(_kv_header.state == PARAM_MODIFIED) {
            /* Change value: Check modify list */
            uint8_t *_copy_data = s_calloc(2, _package_len);
            if(NULL == _copy_data) {
                goto NO_MEMORY;
            }
            /* Change param state */
            _kv_header.state = PARAM_CORRECT;
            memcpy(_copy_data, &_kv_header, sizeof(kv_header_t));
            /* Find out modify template in hash table */
            hash_entry_t *_entry = get_hash_entry(&_kv_header);
            if(NULL == _entry->m_temp) {
                /* No modification record: This situation usually does not occur */
                if(ERR_NONE != read_params_from_sector(_src_addr + sizeof(kv_header_t), &_copy_data[sizeof(kv_header_t)], _data_len)) {
                    s_free(_copy_data);
                    goto READ_FAIL;
                }
            }
            else {
                if(ERR_NONE != read_params_from_sector(_src_addr + sizeof(kv_header_t), &_copy_data[sizeof(kv_header_t)], _kv_header.key_len)) {
                    s_free(_copy_data);
                    goto READ_FAIL;
                }
                memcpy(_copy_data + sizeof(kv_header_t) + _kv_header.key_len, _entry->m_temp, _kv_header.value_len);
            }
            SYSTEM_ERROR_CODE_E __ret = write_params_to_sector(_w_addr, _copy_data, _package_len);
            if(__ret != ERR_NONE) {
                s_ext_store_mgnt.fault_offset = (_w_addr - _w_begin) / EXT_PACKET_SECTOR_BASE_SIZE;
                if((_w_addr - _w_begin) % EXT_PACKET_SECTOR_BASE_SIZE) {
                    s_ext_store_mgnt.fault_offset += 1;
                }
                s_free(_copy_data);
                goto WRITE_FAIL;
            }
            __ret = verify_param_write_result(_w_addr, _copy_data, &_copy_data[_package_len], _package_len);
            if(__ret != ERR_NONE) {
                s_ext_store_mgnt.fault_offset = (_w_addr - _w_begin) / EXT_PACKET_SECTOR_BASE_SIZE;
                if((_w_addr - _w_begin) % EXT_PACKET_SECTOR_BASE_SIZE) {
                    s_ext_store_mgnt.fault_offset += 1;
                }
                s_free(_copy_data);
                goto WRITE_FAIL;
            }
            s_free(_copy_data);
            if(_entry->m_temp) {
                s_free(_entry->m_temp);
                _entry->m_temp = NULL;
            }
            /* Update flash address and param state in the hash table */
            uint32_t _new_rela_addr = _w_addr - _w_begin;
            change_item_info_in_hash_table(&_kv_header, &_kv_header.state, &_new_rela_addr);
            /* Update next write address */
            _w_addr = move_param_addr_cross_sector(_w_addr, _package_len);
        } 
        else if(_kv_header.state == PARAM_DELETE) {
            /* Jump current param */
        }
        _src_addr = move_param_addr_cross_sector(_src_addr, _package_len);
    }
    
    /* Check append list */
    int _n_count = get_list_length(s_ext_store_mgnt.new_list);
    for(int i = 0; i < _n_count; i++) {
        iterator_t _n_it = begin_of_list(s_ext_store_mgnt.new_list);
        hash_cache_t *_cache = get_list_data(_n_it);
        uint32_t _data_len = _cache->header.key_len + _cache->header.value_len;
        uint32_t _package_len = sizeof(kv_header_t) + _data_len;
        uint8_t *_copy_data = s_calloc(2, _package_len);
        if(NULL == _copy_data) {
            goto NO_MEMORY;
        }
        memcpy(_copy_data, &_cache->header, sizeof(kv_header_t));
        memcpy(_copy_data + sizeof(kv_header_t), _cache->key, _cache->header.key_len);
        memcpy(_copy_data + sizeof(kv_header_t) + _cache->header.key_len, _cache->value, _cache->header.value_len);
        SYSTEM_ERROR_CODE_E __ret = write_params_to_sector(_w_addr, _copy_data, _package_len);
        if(__ret != ERR_NONE) {
            s_ext_store_mgnt.fault_offset = (_w_addr - _w_begin) / EXT_PACKET_SECTOR_BASE_SIZE;
            if((_w_addr - _w_begin) % EXT_PACKET_SECTOR_BASE_SIZE) {
                s_ext_store_mgnt.fault_offset += 1;
            }
            s_free(_copy_data);
            goto WRITE_FAIL;
        }
        __ret = verify_param_write_result(_w_addr, _copy_data, &_copy_data[_package_len], _package_len);
        if(__ret != ERR_NONE) {
            s_ext_store_mgnt.fault_offset = (_w_addr - _w_begin) / EXT_PACKET_SECTOR_BASE_SIZE;
            if((_w_addr - _w_begin) % EXT_PACKET_SECTOR_BASE_SIZE) {
                s_ext_store_mgnt.fault_offset += 1;
            }
            s_free(_copy_data);
            goto WRITE_FAIL;
        }
        /* Update flash address and param state in the hash table */
        uint32_t _new_rela_addr = _w_addr - _w_begin;
        uint8_t _state = PARAM_CORRECT;
        change_item_info_in_hash_table(&_cache->header, &_state, &_new_rela_addr);
        s_free(_copy_data);
        if(_cache->key) {
            s_free(_cache->key);
        }
        if(_cache->value) {
            s_free(_cache->value);
        }
        remove_iterator(s_ext_store_mgnt.new_list, _n_it);
        /* Update next write address */
        _w_addr = move_param_addr_cross_sector(_w_addr, _package_len);
    }
    /* Supplement the head and tail information of the new sectors */
    s_ext_store_mgnt.write_count += 1;
    ext_store_packet_start_t _new_packet_start = {0};
    ext_store_packet_end_t _new_packet_end = {0};
    for(int i = 0; i < _next_sector_count; i++) {
        _new_packet_start.uid_start = SAFE_START_VERIFI;
        _new_packet_start.write_count = s_ext_store_mgnt.write_count;
        _new_packet_start.sector_status = SECTOR_ACTIVE;
        _new_packet_start.sector_index = i;
        _new_packet_start.sector_count = _next_sector_count;
        if(s_ext_store_mgnt.next_total_param_size > (i + 1) * EXT_PACKET_PARAM_MINI_SIZE) {
            _new_packet_start.param_size = EXT_PACKET_PARAM_MINI_SIZE;
        } else {
            _new_packet_start.param_size = s_ext_store_mgnt.next_total_param_size % EXT_PACKET_PARAM_MINI_SIZE;
        }
        uint32_t _packet_start_address = _w_begin + (i * EXT_PACKET_SECTOR_BASE_SIZE) + ADDR_SAFE_UID_START;
        if(ERR_NONE != s_ext_store_mgnt.hw_handler.write(_packet_start_address,
            sizeof(ext_store_packet_start_t), (uint8_t *)&_new_packet_start)) {
            goto WRITE_FAIL;
        }
        _new_packet_end.uid_end = SAFE_END_VERIFI;
        /* Cal crc32 */
        _new_packet_end.crc32_backup = UINT32_MAX;
        uint32_t _crc_length = sizeof(ext_store_packet_start_t) + _new_packet_start.param_size;
        _new_packet_end.crc32_active = cal_param_sector_crc32(_packet_start_address, _crc_length);
        uint32_t _packet_end_address = _w_begin + (i * EXT_PACKET_SECTOR_BASE_SIZE) + ADDR_SECTOR_CRC32_BACKUP;
        if(ERR_NONE != s_ext_store_mgnt.hw_handler.write(_packet_end_address,
            sizeof(ext_store_packet_end_t), (uint8_t *)&_new_packet_end)) {
            goto WRITE_FAIL;
        }
        ext_store_packet_start_t _verify_packet_start = {0};
        ext_store_packet_end_t _verify_packet_end = {0};
        /* Verify wrte result */
        if(ERR_NONE != verify_directly_write_result(_packet_start_address,
            (uint8_t *)&_new_packet_start, (uint8_t *)&_verify_packet_start, sizeof(ext_store_packet_start_t))) {
            s_ext_store_mgnt.fault_offset = _next_sector_count;
            goto WRITE_FAIL;
        }
        if(ERR_NONE != verify_directly_write_result(_packet_end_address,
            (uint8_t *)&_new_packet_end, (uint8_t *)&_verify_packet_end, sizeof(ext_store_packet_end_t))) {
            s_ext_store_mgnt.fault_offset = _next_sector_count;
            goto WRITE_FAIL;
        }
    }
    
    /* Mark old acitve sectors to backup */
    if(s_ext_store_mgnt.find_active) {
        uint32_t _old_active_addr = s_ext_store_mgnt.start_addr;
        for(int i = 0; i < s_ext_store_mgnt.sector_count; i++) {
            uint16_t backup_status = SECTOR_BACKUP;
            uint32_t _param_size = 0;
            uint32_t _old_sector_address = _old_active_addr + (i * EXT_PACKET_SECTOR_BASE_SIZE);
            uint32_t _param_size_address = _old_sector_address + ADDR_PARAM_SIZE;
            if(ERR_NONE != s_ext_store_mgnt.hw_handler.read(_param_size_address,
                sizeof(uint32_t), (uint8_t *)&_param_size)) {
                goto READ_FAIL;
            }
            uint32_t _status_address = _old_sector_address + ADDR_SECTOR_STATUS;
            if(ERR_NONE != s_ext_store_mgnt.hw_handler.write(_status_address,
                sizeof(uint16_t), (uint8_t *)&backup_status)) {
                goto WRITE_FAIL;
            }
            uint32_t _backup_crc_length = sizeof(ext_store_packet_start_t) + _param_size;
            uint32_t _backup_crc32 = cal_param_sector_crc32(
                _old_sector_address + ADDR_SAFE_UID_START, _backup_crc_length);
            uint32_t _backup_crc_address = _old_sector_address + ADDR_SECTOR_CRC32_BACKUP;
            if(ERR_NONE != s_ext_store_mgnt.hw_handler.write(_backup_crc_address,
                sizeof(uint32_t), (uint8_t *)&_backup_crc32)) {
                goto WRITE_FAIL;
            }
        }
    }
    /* Mark old backup sectors to invalid */
    if(s_ext_store_mgnt.find_active && s_ext_store_mgnt.find_backup) {
        uint32_t _old_backup_addr = s_ext_store_mgnt.backup_start_addr;
        for(int i = 0; i < s_ext_store_mgnt.backup_sector_count; i++) {
            uint16_t invalid_status = SECTOR_INVALID;
            uint32_t _status_address = _old_backup_addr + (i * EXT_PACKET_SECTOR_BASE_SIZE) + ADDR_SECTOR_STATUS;
            if(ERR_NONE != s_ext_store_mgnt.hw_handler.write(_status_address,
                    sizeof(uint16_t), (uint8_t *)&invalid_status)) {
                goto WRITE_FAIL;
            }
        }
    }
    
    /* Change global mgnt info */
    if(s_ext_store_mgnt.find_active && false == s_ext_store_mgnt.find_backup) {
        s_ext_store_mgnt.find_backup = true;
    }
    if(false == s_ext_store_mgnt.find_active) {
        s_ext_store_mgnt.find_active = true;
    }
    s_ext_store_mgnt.backup_start_addr = s_ext_store_mgnt.start_addr;
    s_ext_store_mgnt.backup_sector_count = s_ext_store_mgnt.sector_count;
    s_ext_store_mgnt.start_addr = _w_begin;
    s_ext_store_mgnt.total_param_size = s_ext_store_mgnt.next_total_param_size;
    s_ext_store_mgnt.sector_count = _next_sector_count;
    s_ext_store_mgnt.param_end_addr = EXT_PACKET_SECTOR_BASE_SIZE * (_next_sector_count - 1) + \
                                        sizeof(ext_store_packet_start_t) + (s_ext_store_mgnt.total_param_size % EXT_PACKET_PARAM_MINI_SIZE);
    
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    /* Fault record Zeroing */
    s_ext_store_mgnt.fault_offset = 0;
    return ERR_NONE;

WRITE_FAIL:
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_FLASH_WRITE_FAIL;

READ_FAIL:
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_FLASH_READ_FAIL;
    
NO_MEMORY:
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NO_MEM;
}


SYSTEM_ERROR_CODE_E ext_store_init(ext_flash_handler_t *flash_handler) {
    if(NULL == flash_handler) {
        return ERR_INVALID_POINTER;
    }
    if(NULL == flash_handler->write || NULL == flash_handler->read || NULL == flash_handler->erase) {
        return ERR_INVALID_ARG;
    }
    if(PARAM_AREA_CAPACITY < (2 * EXT_PACKET_SECTOR_BASE_SIZE)) {
        return ERR_INVALID_LEN;
    }
    memcpy(&s_ext_store_mgnt.hw_handler, flash_handler, sizeof(ext_flash_handler_t));
    
    s_ext_store_mgnt.kv_mutex = xSemaphoreCreateMutex();
    if (s_ext_store_mgnt.kv_mutex == NULL) {
        EXT_STORE_LOG_ERROR("Failed to create mutex!");
        return ERR_NO_MEM;
    }
    
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    init_list(&s_ext_store_mgnt.new_list, sizeof(hash_cache_t), s_calloc, s_free);
    if(NULL == s_ext_store_mgnt.new_list) {
        return ERR_NO_MEM;
    }
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    
    s_ext_store_mgnt.is_init = true;
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E kv_get_type(const char *key, uint8_t *value_type) {
    if(key == NULL || value_type == NULL) {
        return ERR_INVALID_POINTER;
    }
    uint16_t _key_len = strlen(key);
    if(0 == _key_len) {
        return ERR_INVALID_LEN;
    }
    uint64_t _hash = HASH_64(key, _key_len);
    /* Get the position in hash table */
    uint16_t _hash_index = _hash % HASH_TABLE_SIZE;
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    hash_entry_t *_current = hash_table[_hash_index];
    while(_current != NULL) {
        if(_current->header.hash != _hash || _current->header.key_len != _key_len) {
            _current = _current->next;
            continue;
        }
#if CHECK_HASH_KEY
        /* Check again : Compare key */
        uint8_t _key_from_flash[_key_len];
        read_params_from_active_sector(_current->rela_addr + sizeof(kv_header_t), _key_from_flash, _key_len);
        if(0 != memcmp((void *)key, _key_from_flash, _key_len)) {
            _current = _current->next;
            continue;
        }
#endif
        *value_type = _current->header.type;
        xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
        return ERR_NONE;
    }
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NOT_EXIST;
}


SYSTEM_ERROR_CODE_E kv_get_len(const char *key, uint16_t *value_len) {
    if(key == NULL || value_len == NULL) {
        return ERR_INVALID_POINTER;
    }
    uint16_t _key_len = strlen(key);
    if(0 == _key_len) {
        return ERR_INVALID_LEN;
    }
    uint64_t _hash = HASH_64(key, _key_len);
    /* Get the position in hash table */
    uint16_t _hash_index = _hash % HASH_TABLE_SIZE;
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    hash_entry_t *_current = hash_table[_hash_index];
    while(_current != NULL) {
        if(_current->header.hash != _hash || _current->header.key_len != _key_len) {
            _current = _current->next;
            continue;
        }
#if CHECK_HASH_KEY
        /* Check again : Compare key */
        uint8_t _key_from_flash[_key_len];
        read_params_from_active_sector(_current->rela_addr + sizeof(kv_header_t), _key_from_flash, _key_len);
        if(0 != memcmp((void *)key, _key_from_flash, _key_len)) {
            _current = _current->next;
            continue;
        }
#endif
        *value_len = _current->header.value_len;
        xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
        return ERR_NONE;
    }
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NOT_EXIST;
}


SYSTEM_ERROR_CODE_E kv_get(const char *key, uint8_t *value_buf, uint16_t value_len) {
    if(key == NULL || value_buf == NULL) {
        return ERR_INVALID_POINTER;
    }
    uint16_t _key_len = strlen(key);
    if(0 == _key_len || 0 == value_len) {
        return ERR_INVALID_LEN;
    }
    uint64_t _hash = HASH_64(key, _key_len);
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    /* Get the position in hash table */
    uint16_t _hash_index = _hash % HASH_TABLE_SIZE;
    hash_entry_t *_current = hash_table[_hash_index];
    while(_current != NULL) {
        if(_current->header.hash != _hash || _current->header.key_len != _key_len) {
            _current = _current->next;
            continue;
        }
#if CHECK_HASH_KEY
        /* Check again : Compare key */
        uint8_t _key_from_flash[_key_len];
        read_params_from_active_sector(_current->rela_addr + sizeof(kv_header_t), _key_from_flash, _key_len);
        if(0 != memcmp((void *)key, _key_from_flash, _key_len)) {
            _current = _current->next;
            continue;
        }
#endif
        if(value_len > _current->header.value_len) {
            xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
            return ERR_INVALID_LEN;
        }
        read_params_from_active_sector(_current->rela_addr + sizeof(kv_header_t) + _key_len, value_buf, value_len);
        xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
        return ERR_NONE;
    }
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NOT_EXIST;
}


SYSTEM_ERROR_CODE_E kv_set(const char *key, uint8_t type, uint8_t *value_buf, uint16_t value_len) {
    if(key == NULL || value_buf == NULL) {
        return ERR_INVALID_POINTER;
    }
    uint16_t _key_len = strlen(key);
    if(0 == _key_len || 0 == value_len) {
        return ERR_INVALID_LEN;
    }
    uint64_t _hash = HASH_64(key, _key_len);
    /* Get the position in hash table */
    uint16_t _hash_index = _hash % HASH_TABLE_SIZE;
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    hash_entry_t *_current = hash_table[_hash_index];
    hash_entry_t *_prev = NULL;
    uint8_t _raw_state = PARAM_NOT_FOUND;
    uint16_t _raw_value_size = 0;
    
    while(_current != NULL) {
        if(_current->header.hash != _hash || _current->header.key_len != _key_len) {
            _prev = _current;
            _current = _current->next;
            continue;
        }
#if CHECK_HASH_KEY
        /* Check again : Compare key */
        uint8_t _key_from_flash[_key_len];
        read_params_from_active_sector(_current->rela_addr + sizeof(kv_header_t), _key_from_flash, _key_len);
        if(0 != memcmp((void *)key, _key_from_flash, _key_len)) {
            _prev = _current;
            _current = _current->next;
            continue;
        }
#endif
        _raw_value_size = _current->header.value_len;
        _raw_state = _current->header.state;
        if(value_len != _current->header.value_len) {
            /* Value length has been changed */
            if(_raw_state != PARAM_DELETE) {
                uint8_t _state = PARAM_DELETE;
                _current->header.state = _state;
                write_params_to_active_sector(_current->rela_addr + sizeof(uint64_t), &_state, sizeof(uint8_t));
            }
        } 
        else {
            /* No change in value legnth  */
            if(_raw_state == PARAM_CORRECT) {
                uint8_t _state = PARAM_MODIFIED;
                _current->header.state = _state;
                write_params_to_active_sector(_current->rela_addr + sizeof(uint64_t), &_state, sizeof(uint8_t));
            }
        }
        break;
    }
    
    /* Malloc value */
    void *_new_value = s_calloc(value_len, sizeof(uint8_t));
    if(NULL == _new_value) {
        xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
        return ERR_NO_MEM;
    }
    memcpy(_new_value, value_buf, value_len);
    
    if((_current != NULL) &&(value_len == _current->header.value_len) &&((_raw_state == PARAM_CORRECT) || (_raw_state == PARAM_MODIFIED))) {
        if(_current->m_temp) {
            s_free(_current->m_temp);
        }
        _current->m_temp = _new_value;
    }
    else if(_raw_state == PARAM_DELETE || _raw_state == PARAM_INIT) {
        /* Find exist data cache in new_list, and replace its' value */
        hash_cache_t *_old_cache = NULL;
        iterator_t it = begin_of_list(s_ext_store_mgnt.new_list);
        int count = get_list_length(s_ext_store_mgnt.new_list);
        for(int i = 0; i < count; i++) {
            hash_cache_t *__data = get_list_data(it);
            if(__data->header.hash == _hash && __data->header.key_len == _key_len) {
                _old_cache = __data;
                break;
            }
            it = get_list_next(it);
        }
        if(NULL == _old_cache) {
            s_free(_new_value);
            xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
            return ERR_NO_MEM;
        }
        _old_cache->header.value_len = value_len;
        _old_cache->header.type = type;
        _old_cache->header.state = PARAM_CORRECT;
        if(_old_cache->value) {
            s_free(_old_cache->value);
        }
        _old_cache->value = _new_value;
        _current->header.value_len = value_len;
        s_ext_store_mgnt.next_total_param_size += ((int16_t)value_len - (int16_t)_raw_value_size);
    }
    else {
        /* Create hash cache */
        hash_cache_t _cache = {0};
        _cache.header.hash = _hash;
        _cache.header.key_len = _key_len;
        _cache.header.value_len = value_len;
        _cache.header.type = type;
        if(_current) {
            _cache.header.state = _current->header.state;
        }
        else {
            _cache.header.state = PARAM_CORRECT;
        }
        _cache.value = _new_value;
        _cache.key = s_calloc(_key_len, sizeof(char));
        if(NULL == _cache.key) {
            s_free(_cache.value);
            xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
            return ERR_NO_MEM;
        }
        memcpy(_cache.key, key, _key_len);
        
        if(_raw_state == PARAM_NOT_FOUND) {
            /* The actual addition: Insert the new hash entry in the front of hash_table */
            hash_entry_t *_new_entry = (hash_entry_t *)s_calloc(1, sizeof(hash_entry_t));
            if(NULL == _new_entry) {
                s_free(_cache.key);
                s_free(_cache.value);
                xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
                return ERR_NO_MEM;
            }
            _new_entry->rela_addr = 0xFFFFFFFF;
            memcpy(&(_new_entry->header), &_cache.header, sizeof(kv_header_t));
            _new_entry->header.state = PARAM_INIT;
            uint32_t hash_index = _cache.header.hash % HASH_TABLE_SIZE;
            _new_entry->next = hash_table[hash_index];
            hash_table[hash_index] = _new_entry;
            s_ext_store_mgnt.next_total_param_size += (sizeof(kv_header_t) + _key_len + value_len);
        }
        else {
            /* Already exists in kv_table */
            if(_raw_state == PARAM_MODIFIED) {
                if(_current->m_temp) {
                    s_free(_current->m_temp);
                }
            }
            _current->rela_addr = 0xFFFFFFFF;
            memcpy(&(_current->header), &_cache.header, sizeof(kv_header_t));
            s_ext_store_mgnt.next_total_param_size += ((int16_t)value_len - (int16_t)_raw_value_size);
        }
        /* Added to New list */
        append_list(s_ext_store_mgnt.new_list, &_cache);
    }

    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NONE;
}


SYSTEM_ERROR_CODE_E kv_delete(const char *key) {
    if(key == NULL) {
        return ERR_INVALID_POINTER;
    }
    uint16_t _key_len = strlen(key);
    if(0 == _key_len) {
        return ERR_INVALID_LEN;
    }
    uint64_t _hash = HASH_64(key, _key_len);
    /* Get the position in hash table */
    uint16_t _hash_index = _hash % HASH_TABLE_SIZE;
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    hash_entry_t *_current = hash_table[_hash_index];
    hash_entry_t *_prev = NULL;
    while(_current != NULL) {
        if(_current->header.hash != _hash || _current->header.key_len != _key_len) {
            _prev = _current;
            _current = _current->next;
            continue;
        }
#if CHECK_HASH_KEY
        /* Check again : Compare key */
        uint8_t _key_from_flash[_key_len];
        read_params_from_active_sector(_current->rela_addr + sizeof(kv_header_t), _key_from_flash, _key_len);
        if(0 != memcmp((void *)key, _key_from_flash, _key_len)) {
            _prev = _current;
            _current = _current->next;
            continue;
        }
#endif
        /* Mark param state invalid */
        uint8_t _state = PARAM_DELETE;
        _current->header.state = _state;
        write_params_to_active_sector(_current->rela_addr + sizeof(uint64_t), &_state, sizeof(uint8_t));
        /* Remove from hash table */
        if(_prev) {
            _prev->next = _current->next;
        } else {
            hash_table[_hash_index] = _current->next;
        }
        s_ext_store_mgnt.next_total_param_size -= sizeof(kv_header_t) + _key_len + _current->header.value_len;
        s_free(_current);
        xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
        return ERR_NONE;
    }
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NOT_EXIST;
}


SYSTEM_ERROR_CODE_E kv_link_cfg_info(const char *key, void *cfg_info) {
    if(key == NULL || cfg_info == NULL) {
        return ERR_INVALID_POINTER;
    }
    uint16_t _key_len = strlen(key);
    if(0 == _key_len) {
        return ERR_INVALID_LEN;
    }
    uint64_t _hash = HASH_64(key, _key_len);
    /* Get the position in hash table */
    uint16_t _hash_index = _hash % HASH_TABLE_SIZE;
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    hash_entry_t *_current = hash_table[_hash_index];
    while(_current != NULL) {
        if(_current->header.hash != _hash || _current->header.key_len != _key_len) {
            _current = _current->next;
            continue;
        }
#if CHECK_HASH_KEY
        /* Check again : Compare key */
        uint8_t _key_from_flash[_key_len];
        read_params_from_active_sector(_current->rela_addr + sizeof(kv_header_t), _key_from_flash, _key_len);
        if(0 != memcmp((void *)key, _key_from_flash, _key_len)) {
            _current = _current->next;
            continue;
        }
#endif
        _current->cfg_info = (uint32_t)cfg_info;
        xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
        return ERR_NONE;
    }
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NOT_EXIST;
}


SYSTEM_ERROR_CODE_E kv_get_cfg_info(const char *key, void **dist_cfg_info) {
    if(key == NULL || dist_cfg_info == NULL) {
        return ERR_INVALID_POINTER;
    }
    uint16_t _key_len = strlen(key);
    if(0 == _key_len) {
        return ERR_INVALID_LEN;
    }
    uint64_t _hash = HASH_64(key, _key_len);
    /* Get the position in hash table */
    uint16_t _hash_index = _hash % HASH_TABLE_SIZE;
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    hash_entry_t *_current = hash_table[_hash_index];
    while(_current != NULL) {
        if(_current->header.hash != _hash || _current->header.key_len != _key_len) {
            _current = _current->next;
            continue;
        }
#if CHECK_HASH_KEY
        /* Check again : Compare key */
        uint8_t _key_from_flash[_key_len];
        read_params_from_active_sector(_current->rela_addr + sizeof(kv_header_t), _key_from_flash, _key_len);
        if(0 != memcmp((void *)key, _key_from_flash, _key_len)) {
            _current = _current->next;
            continue;
        }
#endif
        *dist_cfg_info = (void *)_current->cfg_info;
        xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
        return ERR_NONE;
    }
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NOT_EXIST;
}


SYSTEM_ERROR_CODE_E kv_store_init(void) {
    kv_header_t _kv_header = {0};
    uint32_t _r_addr = s_ext_store_mgnt.start_addr + ADDR_BASIC_PARAM;
    uint32_t _end_addr = _r_addr + s_ext_store_mgnt.param_end_addr;
    
    xSemaphoreTake(s_ext_store_mgnt.kv_mutex, portMAX_DELAY);
    while(_r_addr < _end_addr) {
        /* Read kv header */
        SYSTEM_ERROR_CODE_E _ret = read_params_from_sector(_r_addr, (uint8_t *)&_kv_header, sizeof(kv_header_t));
        if(ERR_NONE != _ret) {
            EXT_STORE_LOG_ERROR("Read params fail: 0x%08x", _r_addr);
            _r_addr = move_param_addr_cross_sector(_r_addr, sizeof(kv_header_t));
            continue;
        }
        /* No ask value type is PARAM_CORRECT any more */
        if(_kv_header.key_len == 0 || _kv_header.value_len == 0) {
            _r_addr = move_param_addr_cross_sector(_r_addr, sizeof(kv_header_t));
            continue;
        }
        /* Must not exist in hash table */
        if(get_kv_in_hash_table(_kv_header.hash)) {
            char _dup_key[STORE_KEY_MAX_LEN] = {0};
            read_params_from_sector(_r_addr + sizeof(kv_header_t), (uint8_t *)_dup_key, _kv_header.key_len);
            EXT_STORE_LOG_WARNING("Found duplicate hash, key name: [%s]", _dup_key);
            _r_addr = move_param_addr_cross_sector(_r_addr, sizeof(kv_header_t));
            continue;
        }
        hash_entry_t *new_entry = (hash_entry_t *)s_calloc(1, sizeof(hash_entry_t));
        if(NULL == new_entry) {
            xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
            return ERR_NO_MEM;
        }
        new_entry->rela_addr = _r_addr - s_ext_store_mgnt.start_addr;
        memcpy(&(new_entry->header), &_kv_header, sizeof(kv_header_t));
        uint32_t hash_index = _kv_header.hash % HASH_TABLE_SIZE;
        /* Insert in the front of hash_table */
        new_entry->next = hash_table[hash_index];
        hash_table[hash_index] = new_entry;
        uint32_t _kv_size = sizeof(kv_header_t) + _kv_header.key_len + _kv_header.value_len;
        _r_addr = move_param_addr_cross_sector(_r_addr, _kv_size);
    }
    xSemaphoreGive(s_ext_store_mgnt.kv_mutex);
    return ERR_NONE;
}



#ifndef __UTIL_H__
#define __UTIL_H__

#include "stdbool.h"
#include "stdint.h"
#include "stdlib.h"


typedef enum {
    UNDEFINE_TYPE = 0,
    US8_TYPE,
    IS8_TYPE,
    US16_TYPE,
    IS16_TYPE,
    US32_TYPE,
    IS32_TYPE,
    FL32_TYPE,
    STRING_TYPE
} VAR_TYPE_E;

typedef union {
    uint8_t us8[2];
    int8_t is8[2];
    uint16_t us16;
    int16_t is16;
} FORMAT_2BYTES_U;

typedef union {
    uint8_t us8[4];
    int8_t is8[4];
    uint16_t us16[2];
    int16_t is16[2];
    uint32_t us32;
    int32_t is32;
    float fl32;
} FORMAT_4BYTES_U;

typedef union {
    uint8_t us8[8];
    int8_t is8[8];
    uint16_t us16[4];
    int16_t is16[4];
    uint32_t us32[2];
    int32_t is32[2];
    float fl32[2];
    uint64_t us64;
    int64_t is64;
    double dl32;
} FORMAT_8BYTES_U;


#define GET_MAX(a, b)   (((a) > (b)) ? (a) : (b))
#define GET_MIN(a, b)   (((a) < (b)) ? (a) : (b))

#define US_TO_MS(us)    ((us) / 1000)
#define MS_TO_US(ms)    ((ms) * 1000)
#define US_TO_DUS(us)   ((us) * 10)
#define DUS_TO_US(dus)  ((dus) / 10)
#define DUS_TO_MS(dus)  ((dus) / 10000)

#define V_TO_MV(v)      ((v) * 1000)
#define MV_TO_V(mv)     ((mv) / 1000)

#define A_TO_MA(a)      ((a) * 1000)
#define MA_TO_A(ma)     ((ma) / 1000)

inline bool is_meas_in_abs_err(int32_t value, int32_t ref, int32_t err) {
    return abs((int32_t)(value - ref)) < err;
}


#define MAX_LEN 32
#define MAX_PARTS 5

typedef enum {
    CONV_OK = 0,
    CONV_OVERFLOW,
    CONV_INVALID_CHAR,
    CONV_INVALID_FORMAT
} CONV_ERROR_E;

bool is_valid_number(const char *str);

int32_t str_to_int32(const char *str, CONV_ERROR_E *error);

float str_to_float(const char *str, CONV_ERROR_E *error);

uint32_t parse_hexadecimal(const char *str);

void split_string(const char *str, char parts[MAX_PARTS][MAX_LEN], int *count);


#endif

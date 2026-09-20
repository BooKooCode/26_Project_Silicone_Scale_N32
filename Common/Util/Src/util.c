#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include "util.h"


bool is_valid_number(const char *str) {
    if (str == NULL || *str == '\0') 
        return false;
    if (*str == '+' || *str == '-') 
        str++;
    if (*str == '\0') 
        return false;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
        if (*str == '\0') 
            return false;
        while (*str && *str != '\n' && *str != '\r') {
            if (!isxdigit((unsigned char)*str)) 
                return false;
            str++;
        }
        return true;
    }

    int has_digit = 0;
    int has_dot = 0;
    int has_exp = 0;
    int exp_need_digit = 0;
    while (*str && *str != '\n' && *str != '\r') {
        char c = *str;
        if (isdigit((unsigned char)c)) {
            has_digit = 1;
            if (has_exp)
                exp_need_digit = 0;
        } 
        else if (c == '.') {
            if (has_dot || has_exp) 
                return false;
            has_dot = 1;
        } 
        else if (c == 'e' || c == 'E') {
            if (has_exp || !has_digit) 
                return false;
            has_exp = 1;
            exp_need_digit = 1;
            if (str[1] == '+' || str[1] == '-')
                str++;
            if (!isdigit((unsigned char)str[1])) 
                return false;
        } 
        else {
            return false;
        }
        str++;
    }
    return has_digit && !exp_need_digit;
}


int32_t str_to_int32(const char *str, CONV_ERROR_E *error) {
    char *endptr;
    long result;
    *error = CONV_OK;
    if (strlen(str) > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        result = strtol(str, &endptr, 16);
    } 
    else {
        result = strtol(str, &endptr, 10);
    }
    if (*endptr != '\0' && *endptr != '\n' && *endptr != '\r') {
        *error = CONV_INVALID_CHAR;
        return 0;
    }
    if (result > INT32_MAX) {
        *error = CONV_OVERFLOW;
        return INT32_MAX;
    }
    if (result < INT32_MIN) {
        *error = CONV_OVERFLOW;
        return INT32_MIN;
    }
    return (int32_t)result;
}


float str_to_float(const char *str, CONV_ERROR_E *error) {
    const char *cursor = str;
    uint32_t significand = 0U;
    uint32_t significant_digits = 0U;
    int32_t decimal_exponent = 0;
    int32_t parsed_exponent = 0;
    bool negative = false;
    bool exponent_negative = false;
    bool decimal_seen = false;
    bool digit_seen = false;
    bool significant_digit_seen = false;

    *error = CONV_OK;

    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        ++cursor;
    }

    while ((*cursor >= '0' && *cursor <= '9') || *cursor == '.') {
        if (*cursor == '.') {
            if (decimal_seen) {
                *error = CONV_INVALID_CHAR;
                return 0.0f;
            }
            decimal_seen = true;
            ++cursor;
            continue;
        }

        const uint32_t digit = (uint32_t)(*cursor - '0');
        digit_seen = true;
        if (!significant_digit_seen && digit == 0U) {
            if (decimal_seen) {
                --decimal_exponent;
            }
        }
        else {
            significant_digit_seen = true;
            if (significant_digits < 9U) {
                significand = (significand * 10U) + digit;
                ++significant_digits;
                if (decimal_seen) {
                    --decimal_exponent;
                }
            }
            else if (!decimal_seen) {
                ++decimal_exponent;
            }
        }
        ++cursor;
    }

    if ((*cursor == 'e' || *cursor == 'E') && digit_seen) {
        ++cursor;
        if (*cursor == '+' || *cursor == '-') {
            exponent_negative = (*cursor == '-');
            ++cursor;
        }
        if (*cursor < '0' || *cursor > '9') {
            *error = CONV_INVALID_CHAR;
            return 0.0f;
        }
        while (*cursor >= '0' && *cursor <= '9') {
            if (parsed_exponent < 1000) {
                parsed_exponent = (parsed_exponent * 10) + (*cursor - '0');
            }
            ++cursor;
        }
        decimal_exponent += exponent_negative ? -parsed_exponent : parsed_exponent;
    }

    if (!digit_seen || (*cursor != '\0' && *cursor != '\n' && *cursor != '\r')) {
        *error = CONV_INVALID_CHAR;
        return 0.0f;
    }

    float result = (float)significand;
    while (decimal_exponent > 0) {
        if (result > FLT_MAX / 10.0f) {
            *error = CONV_OVERFLOW;
            return negative ? -FLT_MAX : FLT_MAX;
        }
        result *= 10.0f;
        --decimal_exponent;
    }
    while (decimal_exponent < 0 && result != 0.0f) {
        result /= 10.0f;
        ++decimal_exponent;
    }

    if (result != 0.0f && result < FLT_MIN) {
        *error = CONV_OVERFLOW;
        return negative ? -FLT_MIN : FLT_MIN;
    }
    return negative ? -result : result;
}


uint32_t parse_hexadecimal(const char *str) {
    // Use strtoul to convert the hexadecimal string to an unsigned long
    return strtoul(str, NULL, 16);
}


void split_string(const char *str, char parts[MAX_PARTS][MAX_LEN], int *count) {
    int i = 0;
    const char *start = str;
    const char *end;
    
    while ((end = strchr(start, ',')) != NULL) {
        size_t len = end - start;
        if (len >= MAX_LEN) len = MAX_LEN - 1; // Ensure not to overflow
        strncpy(parts[i], start, len);
        parts[i][len] = '\0'; // Null terminate
        i++;
        start = end + 1;
    }

    // Copy the last part after the last comma
    strncpy(parts[i], start, MAX_LEN - 1);
    parts[i][MAX_LEN - 1] = '\0'; // Null terminate
    i++;

    *count = i;
}

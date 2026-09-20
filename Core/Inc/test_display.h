#ifndef TEST_DISPLAY_H
#define TEST_DISPLAY_H

#include <stdint.h>

void test_display_init(void);
void test_display_update(void);
void test_display_show_battery(uint8_t percent);
void test_display_shutdown(void);

#endif

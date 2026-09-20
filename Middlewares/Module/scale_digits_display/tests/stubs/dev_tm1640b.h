#ifndef TEST_DEV_TM1640B_H
#define TEST_DEV_TM1640B_H

#include <stdbool.h>
#include <stdint.h>

typedef int32_t ret_code_t;

#define NS_SUCCESS 0

ret_code_t dev_tm1640b_senddata(uint8_t start_addr, uint8_t const *data, uint8_t len);
ret_code_t dev_tm1640b_onoff_ctrl(bool enabled);
void dev_tm1640b_sleeping(void);
void dev_tm1640b_init(void);
ret_code_t dev_tm1640b_wakeup(void);
void dev_tm1640b_init_software(void);
void dev_tm1640b_wakeup_software(void);
void dev_tm1640b_senddata_software(uint8_t start_addr, uint8_t const *data, uint8_t len);
void dev_tm1640b_onoff_ctrl_software(bool enabled);

#endif
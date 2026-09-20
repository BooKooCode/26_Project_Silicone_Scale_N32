#ifndef __DEV_NAME_H__
#define __DEV_NAME_H__

#include <stdint.h>

#define DEV_NAME_TEMPLATE           "BOOKOO_SC_U 000000"
#define DEV_NAME_SIZE               (sizeof(DEV_NAME_TEMPLATE) - 1)

uint32_t gen_device_name_rn(void);

#endif



#include "bookoo_error_def.h"



void app_warning_handler_bare(uint16_t _warning_code) {
	uint32_t warning_code = _warning_code;
	uint32_t lr = (uint32_t)((uint32_t *)(warning_code)) + 0x00;

}

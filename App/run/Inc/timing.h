#ifndef TIMING_H__
#define TIMING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum _timing_mgnt_req_e {
	TIMING_START_REQ = 0U,
	TIMING_STOP_REQ,
	TIMING_RESET_REQ,
	TIMING_FORCE_RESET_REQ,
} timing_mgnt_req_e;

typedef enum _timing_mgnt_state_e {
	TIMING_GOING = 0U,
	TIMING_STOPPED,
	TIMING_RESETED,
} timing_mgnt_state_e;


void timing_mgnt_req(timing_mgnt_req_e _req);
uint32_t timing_mgnt_get_pcount(void);
timing_mgnt_state_e timing_mgnt_getstate(void);
void timing_mgnt_set_initcount(uint32_t const _count);

#ifdef __cplusplus
}
#endif

#endif	// TIMING_H__

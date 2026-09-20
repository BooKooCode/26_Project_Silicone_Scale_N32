#ifndef XOR_VERIFY_H__
#define XOR_VERIFY_H__

#ifdef __cplusplus
extern "C" {
#endif
	
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

static uint8_t calc_xor_verify_code(const uint8_t * arr, uint16_t len){
	uint8_t res = 0;
	
	if(arr == NULL || len == 0){
		return res;
	}
	
	res = arr[0];
	if(len == 1){
		return res;
	}
	
	for(uint16_t i = 0;i < len-1;i++){
		res = res ^ arr[i+1];
	}
	
	return res;
}

static bool xor_verify(const uint8_t * arr, uint16_t len, uint8_t xor_be_verifid){
	uint8_t xor_code = calc_xor_verify_code(arr, len);
	
	if(xor_code == xor_be_verifid){
		return true;
	}
	return false;
}

#ifdef __cplusplus
}
#endif

#endif

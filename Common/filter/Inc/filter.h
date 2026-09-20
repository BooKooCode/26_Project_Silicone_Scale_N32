#ifndef FILTERS_H__
#define FILTERS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GEN_MEAN_FILTER(_name, _n, _type)	\
	static _type mean_buf_ ## _name[_n] = {0};	\
	static uint16_t mean_cnt ## _name = 0;	\
	const uint16_t mean_n ## _name = _n;	\
	\
	void mean_filter_handler_ ## _name (_type * _x, _type * _y){	\
		uint16_t n = mean_n ## _name;	\
		\
		mean_buf_ ## _name[mean_cnt ## _name] = *_x;	\
		mean_cnt ## _name ++;	\
		if((mean_cnt ## _name % n) == 0){	\
			mean_cnt ## _name = 0;	\
		}	\
		\
		_type sum = (_type)(0);\
		for(uint16_t i = 0;i < n;i++){	\
			sum += mean_buf_ ## _name[i];	\
		}	\
		*_y = (sum / n);	\
	}	\

#define GEN_KALMAN_FILTER_1D(_name, _a, _b, _h, _q, _r, _type)	\
	static _type kalman1d_a ## _name = _a;	\
	static _type kalman1d_b ## _name = _b;	\
	static _type kalman1d_h ## _name = _h;	\
	static _type kalman1d_q ## _name = _q;	\
	static _type kalman1d_r ## _name = _r;	\
	static _type kalman1d_x ## _name = 0;	\
	static _type kalman1d_g ## _name = 0;	\
	static _type kalman1d_p ## _name = 0;	\
	\
	void kalman_filter1d_handler_ ## _name (_type * _x, _type * _y)	\
	{	\
		kalman1d_x ## _name = kalman1d_a ## _name * kalman1d_x ## _name + kalman1d_b ## _name * (*_x);	\
		kalman1d_p ## _name = kalman1d_a ## _name * kalman1d_p ## _name * kalman1d_a ## _name + kalman1d_q ## _name;	\
		kalman1d_g ## _name = kalman1d_p ## _name * kalman1d_h ## _name / (kalman1d_h ## _name * kalman1d_p ## _name * kalman1d_h ## _name + kalman1d_r ## _name);	\
		\
		kalman1d_x ## _name = kalman1d_x ## _name + kalman1d_g ## _name * ((*_x) - kalman1d_h ## _name * kalman1d_x ## _name);	\
		kalman1d_p ## _name = (1 - kalman1d_g ## _name * kalman1d_h ## _name) * kalman1d_p ## _name;			\
		\
		*_y = kalman1d_x ## _name;	\
	}	\
	\
	void kalman_filter1d_reset_ ## _name (_type _value)	\
	{	\
		kalman1d_x ## _name = _value;	\
		kalman1d_g ## _name = 0;	\
		kalman1d_p ## _name = 0;	\
	}	\
	
#ifdef __cplusplus
}
#endif

#endif	// FILTERS_H__

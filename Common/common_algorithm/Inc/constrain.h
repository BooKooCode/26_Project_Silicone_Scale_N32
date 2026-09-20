#ifndef CONSTRAIN_H__
#define CONSTRAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#define CONSTRAIN_DEF(_name, _type, _min, _max)	\
	const _type _name ## _type ## _##min = _min;	\
	const _type _name ## _type ## _##max = _max;	\
	\
	void constrain_handler_ ## _name ## _##_type (_type * _x, _type * _y)	\
	{	\
		_type x  = *_x;	\
	\
		if(x < _name ## _type ## _##min){	\
			*_y = _name ## _type ## _##min;	\
		}														\
		else if(x > _name ## _type ## _##max){	\
			*_y = _name ## _type ## _##max;	\
		}	\
		else{	\
			*_y = x;	\
		}	\
	}	\

#ifdef __cplusplus
}
#endif

#endif	// CONSTRAIN_H__

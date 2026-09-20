#ifndef __REGULAR_H__
#define __REGULAR_H__

#define NRF_FLOAT_8VALID_MARKER "%s%d.%08d"

#define NRF_FLOAT_8VALID(val) (uint32_t)(((val) < 0 && (val) > -1.0) ? "-" : ""),   \
                              (int32_t)(val),                                       \
                              (int32_t)((((val) > 0) ? (val) - (int32_t)(val)       \
                                                    : (int32_t)(val) - (val))*100000000)

#endif


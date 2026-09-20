#ifndef __BESSEL2_H__
#define __BESEEL2_H__

#include <stdint.h>

typedef struct {
    float b0, b1, b2;
    float a1, a2;

    float x1, x2;
    float y1, y2;
    float fs;
    float fc;
} BesselFilter2nd;


void bessel2_init(BesselFilter2nd* filter, float fs, float fc);

void bessel2_reset(BesselFilter2nd* filter, float initial_value);

float bessel2_update(BesselFilter2nd* filter, float input);

#endif


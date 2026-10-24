// LED colour helpers

#pragma once

#include <cmath>

// Hue 0..1 around the colour wheel, full saturation and value.
inline void Hue(float h, float *r, float *g, float *b)
{
    float x = h * 6.0f;
    *r      = fminf(fmaxf(fabsf(x - 3.0f) - 1.0f, 0.0f), 1.0f);
    *g      = fminf(fmaxf(2.0f - fabsf(x - 2.0f), 0.0f), 1.0f);
    *b      = fminf(fmaxf(2.0f - fabsf(x - 4.0f), 0.0f), 1.0f);
}

/* Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * The minimal C library only declares sqrt(); the functions are in the
 * toolchain's libm, the classification macros are compiler builtins.
 */
#pragma once
#include_next <math.h>

#define NAN        __builtin_nanf("")
#define INFINITY   __builtin_inff()
#define HUGE_VAL   __builtin_huge_val()
#ifndef M_PI
#define M_PI       3.14159265358979323846
#endif
#ifndef M_E
#define M_E        2.71828182845904523536
#endif

#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define signbit(x)    __builtin_signbit(x)
#define fpclassify(x) __builtin_fpclassify(0, 1, 4, 3, 2, (x))

float fabsf(float); float copysignf(float, float);
float sinf(float); float cosf(float); float tanf(float);
float asinf(float); float acosf(float); float atanf(float); float atan2f(float, float);
float sinhf(float); float coshf(float); float tanhf(float);
float asinhf(float); float acoshf(float); float atanhf(float);
float expf(float); float expm1f(float); float logf(float); float log10f(float);
float log2f(float); float log1pf(float); float powf(float, float);
float floorf(float); float ceilf(float); float truncf(float); float roundf(float);
float fmodf(float, float); float modff(float, float *);
float frexpf(float, int *); float ldexpf(float, int);
float erff(float); float erfcf(float); float lgammaf(float); float tgammaf(float);
float hypotf(float, float); float cbrtf(float);
float nearbyintf(float); float rintf(float); long lrintf(float);
float fmaxf(float, float); float fminf(float, float); float exp2f(float);
float remainderf(float, float); float nanf(const char *);
double fabs(double); double floor(double); double ceil(double);
double exp(double); double log(double); double pow(double, double);
double sin(double); double cos(double); double fmod(double, double);

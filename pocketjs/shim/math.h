/* SPDX-License-Identifier: Apache-2.0 */
/*
 * The minimal C library only declares sqrt(); the functions are in the
 * toolchain's libm, the classification macros are compiler builtins.
 */
#pragma once
#include_next <math.h>

#define NAN __builtin_nanf("")
#define INFINITY __builtin_inff()
#define HUGE_VAL __builtin_huge_val()
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf_sign(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)

double fabs(double);
double copysign(double, double);
double floor(double);
double ceil(double);
double trunc(double);
double round(double);
double rint(double);
double nearbyint(double);
long lrint(double);
double fmod(double, double);
double remainder(double, double);
double frexp(double, int *);
double ldexp(double, int);
double scalbn(double, int);
double modf(double, double *);
double exp(double);
double exp2(double);
double expm1(double);
double log(double);
double log2(double);
double log10(double);
double log1p(double);
double pow(double, double);
double cbrt(double);
double hypot(double, double);
double sin(double);
double cos(double);
double tan(double);
double asin(double);
double acos(double);
double atan(double);
double atan2(double, double);
double sinh(double);
double cosh(double);
double tanh(double);
double asinh(double);
double acosh(double);
double atanh(double);
double fmax(double, double);
double fmin(double, double);
double fma(double, double, double);
float fabsf(float);
float floorf(float);
float ceilf(float);
float sqrtf(float);

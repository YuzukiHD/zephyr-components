#ifndef NCNN_SHIM_MATH_H
#define NCNN_SHIM_MATH_H
#include_next <math.h>
#ifdef __cplusplus
extern "C" {
#endif
float expf(float); float logf(float); float tanhf(float); float fmaxf(float, float);
float fminf(float, float); float roundf(float); float floorf(float); float ceilf(float);
float atan2f(float, float); float atanf(float); float sinf(float); float cosf(float);
float tanf(float); float asinf(float); float acosf(float); float sinhf(float);
float coshf(float); float asinhf(float); float acoshf(float); float atanhf(float);
float powf(float, float); float sqrtf(float); float fabsf(float); float truncf(float);
float fmodf(float, float); float log10f(float); float log2f(float); float exp2f(float);
float erff(float); float erfcf(float); float copysignf(float, float); float rintf(float);
float nearbyintf(float); long lrintf(float); float cbrtf(float); float hypotf(float, float);
float remainderf(float, float); float expm1f(float); float log1pf(float);
double floor(double); double ceil(double); double round(double); double trunc(double);
double fmod(double, double); double exp(double); double log(double); double pow(double, double);
double tanh(double); double sin(double); double cos(double); double tan(double);
double atan(double); double atan2(double, double); double fabs(double); double log10(double);
double erf(double);
#ifdef __cplusplus
}
#endif
#endif

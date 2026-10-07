/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOCHS_SHIM_MATH_H_
#define BOCHS_SHIM_MATH_H_
#ifdef __cplusplus
extern "C" {
#endif
double exp(double x);
double log(double x);
double pow(double x, double y);
double sqrt(double x);
double floor(double x);
double ceil(double x);
#ifdef __cplusplus
}
#endif
#endif

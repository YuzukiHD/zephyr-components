/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOCHS_SHIM_SYS_TIME_H_
#define BOCHS_SHIM_SYS_TIME_H_
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef _TIMEVAL_DEFINED
#define _TIMEVAL_DEFINED
struct timeval {
	time_t tv_sec;
	long tv_usec;
};
#endif
int gettimeofday(struct timeval *tv, void *tz);
#ifdef __cplusplus
}
#endif
#endif

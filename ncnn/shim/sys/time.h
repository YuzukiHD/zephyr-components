#ifndef NCNN_SHIM_SYS_TIME_H
#define NCNN_SHIM_SYS_TIME_H
#include <time.h>
struct timeval {
	long tv_sec;
	long tv_usec;
};
#ifdef __cplusplus
extern "C" {
#endif
int gettimeofday(struct timeval *tv, void *tz);
#ifdef __cplusplus
}
#endif
#endif

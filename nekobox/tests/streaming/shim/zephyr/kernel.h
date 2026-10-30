/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#define MIN(a, b) ((a) < (b) ? (a) : (b))
static inline void k_msleep(unsigned ms) { usleep(ms * 1000); }
static inline int64_t k_uptime_get(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

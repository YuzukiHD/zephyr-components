/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <time.h>
#include <sys/_timeval.h>

int gettimeofday(struct timeval *tv, void *tz);

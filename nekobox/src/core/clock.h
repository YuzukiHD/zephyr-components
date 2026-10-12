/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The date and the time of day, from the real time clock */

#ifndef NEKOBOX_CORE_CLOCK_H_
#define NEKOBOX_CORE_CLOCK_H_

#include <stdbool.h>
#include <time.h>

void clock_now(struct tm *tm);
void clock_set(int hour, int minute);
/** Sets the date and the time (year since 1900, month 0..11) */
void clock_set_tm(const struct tm *tm);
/** False when the board has no usable real time clock */
bool clock_valid(void);

#endif /* NEKOBOX_CORE_CLOCK_H_ */

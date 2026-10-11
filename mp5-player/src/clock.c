/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* A software clock: the board has no RTC, so the time counts from the build time */

#include <string.h>
#include <zephyr/kernel.h>

#include "mp5.h"

/* seconds since the epoch at uptime 0 */
static int64_t epoch_at_boot;
static bool initialised;

static int64_t days_from_civil(int y, int m, int d)
{
	int era, yoe, doy, doe;

	y -= m <= 2;
	era = (y >= 0 ? y : y - 399) / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

	return (int64_t)era * 146097 + doe - 719468;
}

/* __DATE__ is "Oct 11 2026", __TIME__ is "11:39:00" */
static int64_t build_time(void)
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	const char *d = __DATE__, *t = __TIME__;
	int m = 1, day, year, hh, mm, ss;

	for (int i = 0; i < 12; i++) {
		if (strncmp(d, months + i * 3, 3) == 0) {
			m = i + 1;
		}
	}
	day = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0');
	year = (d[7] - '0') * 1000 + (d[8] - '0') * 100 + (d[9] - '0') * 10 + (d[10] - '0');
	hh = (t[0] - '0') * 10 + (t[1] - '0');
	mm = (t[3] - '0') * 10 + (t[4] - '0');
	ss = (t[6] - '0') * 10 + (t[7] - '0');

	return days_from_civil(year, m, day) * 86400 + hh * 3600 + mm * 60 + ss;
}

static int64_t now_s(void)
{
	if (!initialised) {
		epoch_at_boot = build_time();
		initialised = true;
	}

	return epoch_at_boot + k_uptime_get() / 1000;
}

void clock_now(struct tm *tm)
{
	int64_t s = now_s();
	int64_t days = s / 86400;
	int sod = (int)(s % 86400);
	int z, era, doe, yoe, y, doy, mp, d, m;

	/* civil_from_days */
	z = (int)days + 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	y = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	d = doy - (153 * mp + 2) / 5 + 1;
	m = mp + (mp < 10 ? 3 : -9);
	y += m <= 2;

	memset(tm, 0, sizeof(*tm));
	tm->tm_year = y - 1900;
	tm->tm_mon = m - 1;
	tm->tm_mday = d;
	tm->tm_hour = sod / 3600;
	tm->tm_min = sod / 60 % 60;
	tm->tm_sec = sod % 60;
	tm->tm_wday = (int)((days % 7 + 11) % 7);	/* 1970-01-01 was a Thursday */
}

void clock_set(int hour, int minute)
{
	int64_t s = now_s();
	int64_t day_start = s - s % 86400;

	epoch_at_boot = day_start + hour * 3600 + minute * 60 - k_uptime_get() / 1000;
}

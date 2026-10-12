/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The time of day comes from the real time clock of the SoC. It counts from 1970 when the board
 * lost its power (there is no battery on every board), so a date before the build is replaced by
 * the build time; the time is then right to the minute at best and the network corrects it.
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/clock.h"

static const struct device *const rtc = DEVICE_DT_GET(DT_NODELABEL(rtc));
static bool checked, rtc_ok;

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

static void rtc_from_build(struct rtc_time *t)
{
	int64_t s = build_time();
	int64_t days = s / 86400;
	int sod = (int)(s % 86400);
	int z = (int)days + 719468;
	int era = (z >= 0 ? z : z - 146096) / 146097;
	int doe = z - era * 146097;
	int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int y = yoe + era * 400;
	int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int mp = (5 * doy + 2) / 153;
	int d = doy - (153 * mp + 2) / 5 + 1;
	int m = mp + (mp < 10 ? 3 : -9);

	y += m <= 2;
	memset(t, 0, sizeof(*t));
	t->tm_year = y - 1900;
	t->tm_mon = m - 1;
	t->tm_mday = d;
	t->tm_hour = sod / 3600;
	t->tm_min = sod / 60 % 60;
	t->tm_sec = sod % 60;
}

static void check(void)
{
	struct rtc_time t, b;

	if (checked) {
		return;
	}
	checked = true;
	rtc_ok = device_is_ready(rtc);
	if (!rtc_ok) {
		return;
	}
	rtc_from_build(&b);
	if (rtc_get_time(rtc, &t) != 0 || t.tm_year < b.tm_year ||
	    (t.tm_year == b.tm_year && t.tm_mon < b.tm_mon)) {
		/* never set, or the power was gone */
		printk("clock: the rtc is not set, using the build time\n");
		rtc_set_time(rtc, &b);
	}
}

bool clock_valid(void)
{
	check();

	return rtc_ok;
}

void clock_now(struct tm *tm)
{
	struct rtc_time t;

	check();
	memset(tm, 0, sizeof(*tm));
	if (rtc_ok && rtc_get_time(rtc, &t) == 0) {
		tm->tm_year = t.tm_year;
		tm->tm_mon = t.tm_mon;
		tm->tm_mday = t.tm_mday;
		tm->tm_hour = t.tm_hour;
		tm->tm_min = t.tm_min;
		tm->tm_sec = t.tm_sec;
		tm->tm_wday = t.tm_wday;
		tm->tm_yday = t.tm_yday;
	}
}

void clock_set_tm(const struct tm *tm)
{
	struct rtc_time t = {
		.tm_year = tm->tm_year, .tm_mon = tm->tm_mon, .tm_mday = tm->tm_mday,
		.tm_hour = tm->tm_hour, .tm_min = tm->tm_min, .tm_sec = tm->tm_sec,
	};

	check();
	if (rtc_ok) {
		rtc_set_time(rtc, &t);
	}
}

void clock_set(int hour, int minute)
{
	struct tm tm;

	clock_now(&tm);
	tm.tm_hour = hour;
	tm.tm_min = minute;
	tm.tm_sec = 0;
	clock_set_tm(&tm);
}

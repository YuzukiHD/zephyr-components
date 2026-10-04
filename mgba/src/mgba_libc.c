/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* The few math functions the minimal C library lacks and the emulator uses. */

#include <math.h>

#define PI 3.14159265358979323846

static double sin_poly(double x)
{
	/* x in [-pi/2, pi/2], Taylor series to x^13 */
	double x2 = x * x;

	return x * (1.0 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040 +
	       x2 * (1.0 / 362880 + x2 * (-1.0 / 39916800 + x2 * (1.0 / 6227020800.0)))))));
}

double sin(double x)
{
	int n = (int)(x / PI + (x < 0 ? -0.5 : 0.5));

	x -= n * PI;

	return (n & 1) ? -sin_poly(x) : sin_poly(x);
}

double cos(double x)
{
	return sin(x + PI / 2);
}

double floor(double x)
{
	double t = (double)(long long)x;

	return (t > x) ? t - 1.0 : t;
}

float sinf(float x)
{
	return (float)sin(x);
}

float cosf(float x)
{
	return (float)cos(x);
}

float exp2f(float xf)
{
	double x = xf;
	int n = (int)x;
	double f, r = 1.0;

	if (x < 0 && x != n) {
		n--;
	}
	f = (x - n) * 0.69314718055994531;
	/* e^f for f in [0, ln 2), Taylor series to f^10 */
	for (int i = 10; i > 0; i--) {
		r = 1.0 + r * f / i;
	}
	for (; n > 0; n--) {
		r *= 2.0;
	}
	for (; n < 0; n++) {
		r *= 0.5;
	}

	return (float)r;
}

#include <ctype.h>

int strcasecmp(const char *a, const char *b)
{
	while (*a != '\0' && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
		a++;
		b++;
	}

	return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

#include <time.h>

/* Only the cartridge clock and the export of save files use these; the clock
 * is not a calendar here, so a plain day count is enough. */
time_t mktime(struct tm *tm)
{
	return ((time_t)((tm->tm_year - 70) * 365 + tm->tm_mon * 30 + tm->tm_mday) * 24 +
		tm->tm_hour) * 3600 + tm->tm_min * 60 + tm->tm_sec;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
	if (max > 0) {
		s[0] = '\0';
	}

	return 0;
}

float fminf(float a, float b)
{
	return a < b ? a : b;
}

float fmaxf(float a, float b)
{
	return a > b ? a : b;
}

#include <mgba-util/vfs.h>

/* files are read through the file system by the application, not by the core */
struct VFile *VFileOpenFD(const char *path, int flags)
{
	return NULL;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <zephyr/kernel.h>

/* the core is given no files by path: a configuration file is never opened */
FILE *fopen(const char *path, const char *mode)
{
	return NULL;
}

int fclose(FILE *f)
{
	return -1;
}

char *fgets(char *s, int n, FILE *f)
{
	return NULL;
}

int mkdir(const char *path, int mode)
{
	return -1;
}

int gettimeofday(struct timeval *tv, void *tz)
{
	int64_t us = k_uptime_get() * 1000;

	tv->tv_sec = us / 1000000;
	tv->tv_usec = us % 1000000;

	return 0;
}

float strtof(const char *s, char **end)
{
	return (float)strtol(s, end, 10);
}

/* float text conversion with a locale is only needed by the config file */
int ftostr_l(char *str, size_t size, float f, const char *locale)
{
	return snprintf(str, size, "%d", (int)f);
}

float strtof_l(const char *str, char **end, const char *locale)
{
	return strtof(str, end);
}

char *getcwd(char *buf, size_t size)
{
	return NULL;
}

char *getenv(const char *name)
{
	return NULL;
}

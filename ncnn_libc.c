/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * What ncnn expects from the C library and the minimal C library does not
 * provide: a small sscanf() for the network description parser, a clock and
 * sleep for the benchmark, strtok().
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/clock.h>
#include <zephyr/toolchain.h>

/* sscanf(): %d %i %f %s %[set] %n %% with width, '*' and 'l'; spaces in the
 * format skip white space, other characters have to match.
 */

static double parse_double(const char **sp, int width, bool *ok)
{
	const char *s = *sp;
	const char *end = width > 0 ? s + width : (const char *)UINTPTR_MAX;
	double v = 0.0;
	bool neg = false;
	bool digits = false;
	int exp10 = 0;

	if (s < end && (*s == '-' || *s == '+')) {
		neg = *s == '-';
		s++;
	}
	while (s < end && isdigit((unsigned char)*s)) {
		v = v * 10.0 + (*s - '0');
		digits = true;
		s++;
	}
	if (s < end && *s == '.') {
		s++;
		while (s < end && isdigit((unsigned char)*s)) {
			v = v * 10.0 + (*s - '0');
			exp10--;
			digits = true;
			s++;
		}
	}
	if (digits && s < end && (*s == 'e' || *s == 'E')) {
		const char *e = s + 1;
		bool eneg = false;
		int ev = 0;
		bool edigits = false;

		if (e < end && (*e == '-' || *e == '+')) {
			eneg = *e == '-';
			e++;
		}
		while (e < end && isdigit((unsigned char)*e)) {
			ev = ev * 10 + (*e - '0');
			edigits = true;
			e++;
		}
		if (edigits) {
			exp10 += eneg ? -ev : ev;
			s = e;
		}
	}
	*ok = digits;
	if (!digits) {
		return 0.0;
	}
	while (exp10 > 0) {
		v *= 10.0;
		exp10--;
	}
	while (exp10 < 0) {
		v /= 10.0;
		exp10++;
	}
	*sp = s;
	return neg ? -v : v;
}

int sscanf(const char *str, const char *fmt, ...)
{
	va_list ap;
	const char *s = str;
	int count = 0;

	va_start(ap, fmt);

	while (*fmt != '\0') {
		if (isspace((unsigned char)*fmt)) {
			while (isspace((unsigned char)*s)) {
				s++;
			}
			fmt++;
			continue;
		}
		if (*fmt != '%') {
			if (*s != *fmt) {
				break;
			}
			s++;
			fmt++;
			continue;
		}

		fmt++;
		bool suppress = false;
		bool is_long = false;
		int width = 0;

		if (*fmt == '*') {
			suppress = true;
			fmt++;
		}
		while (isdigit((unsigned char)*fmt)) {
			width = width * 10 + (*fmt++ - '0');
		}
		while (*fmt == 'l' || *fmt == 'h') {
			is_long = *fmt == 'l';
			fmt++;
		}

		char conv = *fmt++;

		if (conv == 'n') {
			*va_arg(ap, int *) = (int)(s - str);
			continue;
		}
		if (conv == '%') {
			if (*s != '%') {
				break;
			}
			s++;
			continue;
		}
		if (conv != '[' && conv != 'c') {
			while (isspace((unsigned char)*s)) {
				s++;
			}
		}
		if (*s == '\0') {
			break;
		}

		if (conv == 'd' || conv == 'i') {
			const char *p = s;
			int sign = 1;
			long v = 0;
			bool any = false;
			int left = width > 0 ? width : INT32_MAX;

			if (left > 0 && (*p == '-' || *p == '+')) {
				sign = *p == '-' ? -1 : 1;
				p++;
				left--;
			}
			while (left > 0 && isdigit((unsigned char)*p)) {
				v = v * 10 + (*p - '0');
				p++;
				left--;
				any = true;
			}
			if (!any) {
				break;
			}
			s = p;
			if (!suppress) {
				*va_arg(ap, int *) = (int)(sign * v);
				count++;
			}
		} else if (conv == 'f' || conv == 'e' || conv == 'g') {
			bool ok;
			double v = parse_double(&s, width, &ok);

			if (!ok) {
				break;
			}
			if (!suppress) {
				if (is_long) {
					*va_arg(ap, double *) = v;
				} else {
					*va_arg(ap, float *) = (float)v;
				}
				count++;
			}
		} else if (conv == 's') {
			char *out = suppress ? NULL : va_arg(ap, char *);
			int left = width > 0 ? width : INT32_MAX;

			while (left > 0 && *s != '\0' && !isspace((unsigned char)*s)) {
				if (out) {
					*out++ = *s;
				}
				s++;
				left--;
			}
			if (out) {
				*out = '\0';
				count++;
			}
		} else if (conv == '[') {
			bool negate = false;
			bool set[256] = {false};

			if (*fmt == '^') {
				negate = true;
				fmt++;
			}
			/* a ']' first in the set belongs to it */
			bool first = true;

			while (*fmt != '\0' && (*fmt != ']' || first)) {
				unsigned char lo = (unsigned char)*fmt++;

				first = false;
				if (*fmt == '-' && fmt[1] != ']' && fmt[1] != '\0') {
					unsigned char hi = (unsigned char)fmt[1];

					fmt += 2;
					for (unsigned int c = lo; c <= hi; c++) {
						set[c] = true;
					}
				} else {
					set[lo] = true;
				}
			}
			if (*fmt == ']') {
				fmt++;
			}

			char *out = suppress ? NULL : va_arg(ap, char *);
			int left = width > 0 ? width : INT32_MAX;
			int got = 0;

			while (left > 0 && *s != '\0' && (set[(unsigned char)*s] != negate)) {
				if (out) {
					*out++ = *s;
				}
				s++;
				left--;
				got++;
			}
			if (got == 0) {
				break;
			}
			if (out) {
				*out = '\0';
				count++;
			}
		} else {
			break;
		}
	}

	va_end(ap);
	return count;
}

struct timeval {
	long tv_sec;
	long tv_usec;
};

int gettimeofday(struct timeval *tv, void *tz)
{
	uint64_t cycles = k_cycle_get_64();
	uint64_t hz = sys_clock_hw_cycles_per_sec();

	ARG_UNUSED(tz);
	tv->tv_sec = (long)(cycles / hz);
	tv->tv_usec = (long)((cycles % hz) * 1000000ULL / hz);
	return 0;
}

int usleep(unsigned int usec)
{
	return k_usleep((int32_t)usec);
}

__weak char *strtok(char *str, const char *delim)
{
	static char *saved;

	return strtok_r(str, delim, &saved);
}

/* ncnn can load networks from files; there is no file system here, so only
 * the memory loaders work and these report failure.
 */

FILE *fopen(const char *path, const char *mode)
{
	ARG_UNUSED(path);
	ARG_UNUSED(mode);
	return NULL;
}

int fclose(FILE *f)
{
	ARG_UNUSED(f);
	return -1;
}

int feof(FILE *f)
{
	ARG_UNUSED(f);
	return 1;
}

int fgetc(FILE *f)
{
	ARG_UNUSED(f);
	return -1;
}

char *fgets(char *s, int size, FILE *f)
{
	ARG_UNUSED(s);
	ARG_UNUSED(size);
	ARG_UNUSED(f);
	return NULL;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
	ARG_UNUSED(ptr);
	ARG_UNUSED(size);
	ARG_UNUSED(nmemb);
	ARG_UNUSED(f);
	return 0;
}

int fscanf(FILE *f, const char *format, ...)
{
	ARG_UNUSED(f);
	ARG_UNUSED(format);
	return -1;
}

/* the errno of the toolchain's libm */
int *__errno(void)
{
	return &errno;
}

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* C library and socket functions of the supplicant that the minimal C library lacks */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <sys/stat.h>

#include "aic_sock.h"

__weak FILE *fopen(const char *path, const char *mode)
{
	return NULL;
}

__weak int fclose(FILE *f)
{
	return EOF;
}

__weak int unlink(const char *path)
{
	return -1;
}

__weak int stat(const char *path, struct stat *buf)
{
	return -1;
}

__weak int getchar(void)
{
	return -1;
}

/* %d %i %u %x %o %c %s %n %%, with width, 'l' and '*': what the supplicant parses */
static int vsscanf_(const char *str, const char *fmt, va_list ap)
{
	const char *s = str;
	int count = 0;

	while (*fmt) {
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
		bool skip = false, is_long = false;
		int width = 0;

		if (*fmt == '*') {
			skip = true;
			fmt++;
		}
		while (isdigit((unsigned char)*fmt)) {
			width = width * 10 + (*fmt++ - '0');
		}
		while (*fmt == 'l' || *fmt == 'h') {
			is_long = is_long || *fmt == 'l';
			fmt++;
		}
		char conv = *fmt++;

		if (conv == '%') {
			if (*s++ != '%') {
				break;
			}
			continue;
		}
		if (conv == 'n') {
			if (!skip) {
				*va_arg(ap, int *) = s - str;
			}
			continue;
		}
		if (conv != 'c') {
			while (isspace((unsigned char)*s)) {
				s++;
			}
		}
		if (*s == '\0') {
			return count == 0 ? EOF : count;
		}
		if (conv == 'c') {
			int n = width ? width : 1;
			char *d = skip ? NULL : va_arg(ap, char *);

			while (n-- && *s) {
				if (d) {
					*d++ = *s;
				}
				s++;
			}
		} else if (conv == 's') {
			char *d = skip ? NULL : va_arg(ap, char *);
			int n = width ? width : INT32_MAX;

			while (n-- && *s && !isspace((unsigned char)*s)) {
				if (d) {
					*d++ = *s;
				}
				s++;
			}
			if (d) {
				*d = '\0';
			}
		} else if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' ||
			   conv == 'o') {
			int base = conv == 'x' || conv == 'X' ? 16 : conv == 'o' ? 8 :
				   conv == 'i' ? 0 : 10;
			char tmp[24], *end;
			int n = 0;
			long v;

			/* the width limits the characters taken */
			while (s[n] && !isspace((unsigned char)s[n]) && n < (width ? width : 23) &&
			       n < 23) {
				tmp[n] = s[n];
				n++;
			}
			tmp[n] = '\0';
			v = conv == 'u' || base == 16 || base == 8 ? (long)strtoul(tmp, &end, base) :
								     strtol(tmp, &end, base);
			if (end == tmp) {
				break;
			}
			s += end - tmp;
			if (!skip) {
				if (is_long) {
					*va_arg(ap, long *) = v;
				} else {
					*va_arg(ap, int *) = v;
				}
			}
		} else {
			break;
		}
		if (!skip) {
			count++;
		}
	}

	return count;
}

__weak int sscanf(const char *str, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsscanf_(str, fmt, ap);
	va_end(ap);

	return n;
}

__weak int rename(const char *from, const char *to)
{
	return -1;
}

__weak char *fgets(char *s, int n, FILE *f)
{
	return NULL;
}

/* sign, digits, fraction and an exponent */
__weak double strtod(const char *s, char **end)
{
	const char *p = s;
	double v = 0.0, scale = 1.0;
	bool neg = false, any = false;

	while (isspace((unsigned char)*p)) {
		p++;
	}
	if (*p == '-' || *p == '+') {
		neg = *p++ == '-';
	}
	while (isdigit((unsigned char)*p)) {
		v = v * 10.0 + (*p++ - '0');
		any = true;
	}
	if (*p == '.') {
		p++;
		while (isdigit((unsigned char)*p)) {
			scale /= 10.0;
			v += (*p++ - '0') * scale;
			any = true;
		}
	}
	if (!any) {
		if (end) {
			*end = (char *)s;
		}
		return 0.0;
	}
	if (*p == 'e' || *p == 'E') {
		char *e;
		long ex = strtol(p + 1, &e, 10);

		if (e != p + 1) {
			p = e;
			for (; ex > 0; ex--) {
				v *= 10.0;
			}
			for (; ex < 0; ex++) {
				v /= 10.0;
			}
		}
	}
	if (end) {
		*end = (char *)p;
	}

	return neg ? -v : v;
}

static double ln_(double x)
{
	int e = 0;
	double y, y2, sum = 0.0, term;

	if (x <= 0.0) {
		return -1e300;
	}
	while (x > 1.5) {
		x /= 2.0;
		e++;
	}
	while (x < 0.75) {
		x *= 2.0;
		e--;
	}
	/* ln x = 2 atanh((x - 1) / (x + 1)) */
	y = (x - 1.0) / (x + 1.0);
	y2 = y * y;
	term = y;
	for (int i = 1; i < 40; i += 2) {
		sum += term / i;
		term *= y2;
	}

	return 2.0 * sum + e * 0.6931471805599453;
}

static double exp_(double x)
{
	int k = 0;
	double sum = 1.0, term = 1.0;

	while (x > 0.5) {
		x /= 2.0;
		k++;
	}
	while (x < -0.5) {
		x /= 2.0;
		k++;
	}
	for (int i = 1; i < 20; i++) {
		term *= x / i;
		sum += term;
	}
	while (k--) {
		sum *= sum;
	}

	return sum;
}

__weak double pow(double x, double y)
{
	if (y == 0.0) {
		return 1.0;
	}
	if (x == 0.0) {
		return 0.0;
	}
	if (x < 0.0) {
		/* integer exponents only */
		double r = exp_(y * ln_(-x));

		return ((long)y & 1) ? -r : r;
	}

	return exp_(y * ln_(x));
}

__weak char *strpbrk(const char *s, const char *accept)
{
	for (; *s != '\0'; s++) {
		if (strchr(accept, *s) != NULL) {
			return (char *)s;
		}
	}

	return NULL;
}

__weak int strcasecmp(const char *a, const char *b)
{
	for (; ; a++, b++) {
		int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);

		if (d != 0 || *a == '\0') {
			return d;
		}
	}
}

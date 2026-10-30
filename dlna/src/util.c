/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void sb_init(struct sb *b, char *buf, size_t size)
{
	b->buf = buf;
	b->size = size;
	b->len = 0;
	b->over = false;
	if (size) {
		buf[0] = '\0';
	}
}

void sb_puts(struct sb *b, const char *s)
{
	size_t n = strlen(s);

	if (b->len + n + 1 > b->size) {
		b->over = true;
		return;
	}
	memcpy(b->buf + b->len, s, n + 1);
	b->len += n;
}

void sb_printf(struct sb *b, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (b->len + 1 >= b->size) {
		b->over = true;
		return;
	}
	va_start(ap, fmt);
	n = vsnprintf(b->buf + b->len, b->size - b->len, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= b->size - b->len) {
		b->buf[b->len] = '\0';
		b->over = true;
		return;
	}
	b->len += n;
}

void sb_puts_escaped(struct sb *b, const char *s)
{
	for (; *s; s++) {
		switch (*s) {
		case '<':
			sb_puts(b, "&lt;");
			break;
		case '>':
			sb_puts(b, "&gt;");
			break;
		case '&':
			sb_puts(b, "&amp;");
			break;
		case '"':
			sb_puts(b, "&quot;");
			break;
		case '\'':
			sb_puts(b, "&apos;");
			break;
		default:
			if (b->len + 2 > b->size) {
				b->over = true;
			} else {
				b->buf[b->len++] = *s;
				b->buf[b->len] = '\0';
			}
		}
	}
}

static int utf8_put(char *out, unsigned long c)
{
	if (c < 0x80) {
		out[0] = c;
		return 1;
	}
	if (c < 0x800) {
		out[0] = 0xc0 | (c >> 6);
		out[1] = 0x80 | (c & 0x3f);
		return 2;
	}
	if (c < 0x10000) {
		out[0] = 0xe0 | (c >> 12);
		out[1] = 0x80 | ((c >> 6) & 0x3f);
		out[2] = 0x80 | (c & 0x3f);
		return 3;
	}
	out[0] = 0xf0 | (c >> 18);
	out[1] = 0x80 | ((c >> 12) & 0x3f);
	out[2] = 0x80 | ((c >> 6) & 0x3f);
	out[3] = 0x80 | (c & 0x3f);
	return 4;
}

void xml_unescape(char *s)
{
	char *w = s;

	while (*s) {
		if (*s != '&') {
			*w++ = *s++;
			continue;
		}
		if (!strncmp(s, "&lt;", 4)) {
			*w++ = '<';
			s += 4;
		} else if (!strncmp(s, "&gt;", 4)) {
			*w++ = '>';
			s += 4;
		} else if (!strncmp(s, "&amp;", 5)) {
			*w++ = '&';
			s += 5;
		} else if (!strncmp(s, "&quot;", 6)) {
			*w++ = '"';
			s += 6;
		} else if (!strncmp(s, "&apos;", 6)) {
			*w++ = '\'';
			s += 6;
		} else if (s[1] == '#') {
			char *end;
			unsigned long c = (s[2] == 'x' || s[2] == 'X') ? strtoul(s + 3, &end, 16)
								       : strtoul(s + 2, &end, 10);

			if (*end == ';' && c > 0 && c < 0x110000) {
				w += utf8_put(w, c);
				s = end + 1;
			} else {
				*w++ = *s++;
			}
		} else {
			*w++ = *s++;
		}
	}
	*w = '\0';
}

void fmt_time(uint32_t ms, char *out, size_t size)
{
	uint32_t s = ms / 1000;

	snprintf(out, size, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60),
		 (unsigned)(s % 60));
}

bool parse_time(const char *s, uint32_t *ms)
{
	unsigned long part[3] = {0, 0, 0};
	int n = 0;
	uint32_t frac = 0;
	char *end;

	if (*s == '+') {
		s++;
	}
	while (n < 3) {
		if (!isdigit((unsigned char)*s)) {
			return false;
		}
		part[n++] = strtoul(s, &end, 10);
		s = end;
		if (*s != ':') {
			break;
		}
		s++;
	}
	if (n != 3 || part[1] > 59 || part[2] > 59) {
		return false;
	}
	if (*s == '.') {
		uint32_t scale = 100;

		for (s++; isdigit((unsigned char)*s); s++) {
			frac += (*s - '0') * scale;
			scale /= 10;
		}
	}
	if (*s) {
		return false;
	}
	*ms = (uint32_t)((part[0] * 3600 + part[1] * 60 + part[2]) * 1000 + frac);
	return true;
}

bool str_starts_ci(const char *s, const char *prefix)
{
	for (; *prefix; s++, prefix++) {
		if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
			return false;
		}
	}
	return true;
}

const char *header_find(const char *headers, const char *name)
{
	size_t n = strlen(name);
	const char *p = headers;

	while (p && *p && *p != '\r' && *p != '\n') {
		if (str_starts_ci(p, name) && p[n] == ':') {
			p += n + 1;
			while (*p == ' ' || *p == '\t') {
				p++;
			}
			return p;
		}
		p = strchr(p, '\n');
		if (p) {
			p++;
		}
	}
	return NULL;
}

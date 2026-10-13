/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"

static const char *ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
		p++;
	}

	return p;
}

/* past the closing quote of the string that starts at p */
static const char *skip_string(const char *p)
{
	for (p++; *p != '"'; p++) {
		if (*p == '\0') {
			return NULL;
		}
		if (*p == '\\') {
			p++;
			if (*p == '\0') {
				return NULL;
			}
		}
	}

	return p + 1;
}

const char *js_skip(const char *v)
{
	int depth = 0;

	if (v == NULL) {
		return NULL;
	}
	v = ws(v);
	if (*v == '"') {
		return skip_string(v);
	}
	if (*v != '{' && *v != '[') {
		/* number, true, false, null */
		while (*v != '\0' && strchr(",]} \t\r\n", *v) == NULL) {
			v++;
		}

		return v;
	}
	for (;;) {
		switch (*v) {
		case '\0':
			return NULL;
		case '"':
			v = skip_string(v);
			if (v == NULL) {
				return NULL;
			}
			continue;
		case '{':
		case '[':
			depth++;
			break;
		case '}':
		case ']':
			if (--depth == 0) {
				return v + 1;
			}
			break;
		}
		v++;
	}
}

const char *js_get(const char *obj, const char *key)
{
	size_t klen = strlen(key);
	const char *p;

	if (obj == NULL || *(p = ws(obj)) != '{') {
		return NULL;
	}
	p = ws(p + 1);
	while (*p == '"') {
		const char *end = skip_string(p);
		int match;

		if (end == NULL) {
			return NULL;
		}
		match = (size_t)(end - p) == klen + 2 && strncmp(p + 1, key, klen) == 0;
		p = ws(end);
		if (*p != ':') {
			return NULL;
		}
		p = ws(p + 1);
		if (match) {
			return p;
		}
		p = js_skip(p);
		if (p == NULL) {
			return NULL;
		}
		p = ws(p);
		if (*p != ',') {
			return NULL;
		}
		p = ws(p + 1);
	}

	return NULL;
}

const char *js_path(const char *v, const char *path)
{
	char key[48];

	while (v != NULL && *path != '\0') {
		size_t n = strcspn(path, ".");

		if (n >= sizeof(key)) {
			return NULL;
		}
		memcpy(key, path, n);
		key[n] = '\0';
		v = js_get(v, key);
		path += n + (path[n] == '.');
	}

	return v;
}

const char *js_first(const char *arr)
{
	const char *p;

	if (arr == NULL || *(p = ws(arr)) != '[') {
		return NULL;
	}
	p = ws(p + 1);

	return *p == ']' || *p == '\0' ? NULL : p;
}

const char *js_next(const char *elem)
{
	const char *p = js_skip(elem);

	if (p == NULL || *(p = ws(p)) != ',') {
		return NULL;
	}

	return ws(p + 1);
}

static int hex4(const char *p)
{
	int v = 0;

	for (int i = 0; i < 4; i++) {
		char c = p[i];

		v <<= 4;
		if (c >= '0' && c <= '9') {
			v |= c - '0';
		} else if (c >= 'a' && c <= 'f') {
			v |= c - 'a' + 10;
		} else if (c >= 'A' && c <= 'F') {
			v |= c - 'A' + 10;
		} else {
			return -1;
		}
	}

	return v;
}

static int put_utf8(char *out, unsigned int cp)
{
	if (cp < 0x80) {
		out[0] = cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = 0xc0 | (cp >> 6);
		out[1] = 0x80 | (cp & 0x3f);
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = 0xe0 | (cp >> 12);
		out[1] = 0x80 | ((cp >> 6) & 0x3f);
		out[2] = 0x80 | (cp & 0x3f);
		return 3;
	}
	out[0] = 0xf0 | (cp >> 18);
	out[1] = 0x80 | ((cp >> 12) & 0x3f);
	out[2] = 0x80 | ((cp >> 6) & 0x3f);
	out[3] = 0x80 | (cp & 0x3f);

	return 4;
}

int js_str(const char *v, char *out, size_t size)
{
	size_t n = 0;
	bool cut = false;
	char tmp[4];

	if (v == NULL || *v != '"' || size == 0) {
		return -1;
	}
	for (v++; *v != '"'; v++) {
		int len;

		if (*v == '\0') {
			return -1;
		}
		if (*v != '\\') {
			tmp[0] = *v;
			len = 1;
		} else {
			v++;
			switch (*v) {
			case 'n': tmp[0] = '\n'; len = 1; break;
			case 't': tmp[0] = '\t'; len = 1; break;
			case 'r': tmp[0] = '\r'; len = 1; break;
			case 'b': tmp[0] = '\b'; len = 1; break;
			case 'f': tmp[0] = '\f'; len = 1; break;
			case '"': case '\\': case '/': tmp[0] = *v; len = 1; break;
			case 'u': {
				int cp = hex4(v + 1);

				if (cp < 0) {
					return -1;
				}
				v += 4;
				if (cp >= 0xd800 && cp < 0xdc00 && v[1] == '\\' && v[2] == 'u') {
					int lo = hex4(v + 3);

					if (lo >= 0xdc00 && lo < 0xe000) {
						cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
						v += 6;
					}
				}
				len = put_utf8(tmp, cp);
				break;
			}
			default:
				return -1;
			}
		}
		/* a sequence is never cut in the middle */
		if (n + len >= size) {
			cut = true;
			break;
		}
		memcpy(out + n, tmp, len);
		n += len;
	}
	if (cut) {
		/* a character that came as raw UTF-8 bytes may have been cut: drop what is left of it */
		size_t i = n;

		while (i > 0 && ((unsigned char)out[i - 1] & 0xc0) == 0x80) {
			i--;
		}
		if (i > 0 && (unsigned char)out[i - 1] >= 0xc0) {
			unsigned char lead = out[i - 1];
			size_t need = lead >= 0xf0 ? 4 : (lead >= 0xe0 ? 3 : 2);

			if (n - (i - 1) < need) {
				n = i - 1;
			}
		}
	}
	out[n] = '\0';

	return (int)n;
}

long long js_int(const char *v, long long def)
{
	char *end;
	long long r;

	if (v == NULL || (*v != '-' && (*v < '0' || *v > '9'))) {
		return def;
	}
	r = strtoll(v, &end, 10);

	return end == v ? def : r;
}

int js_is(const char *v, const char *s)
{
	size_t n = strlen(s);

	return v != NULL && *v == '"' && strncmp(v + 1, s, n) == 0 && v[n + 1] == '"';
}

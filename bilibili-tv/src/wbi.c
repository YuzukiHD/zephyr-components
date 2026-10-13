/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "md5.h"
#include "wbi.h"

#define MAX_PARAMS	24

static const uint8_t mixin[64] = {
	46, 47, 18, 2, 53, 8, 23, 32, 15, 50, 10, 31, 58, 3, 45, 35, 27, 43, 5, 49, 33, 9, 42, 19, 29, 28,
	14, 39, 12, 38, 41, 13, 37, 48, 7, 16, 24, 55, 40, 61, 26, 17, 0, 1, 60, 51, 30, 4, 22, 25, 54,
	21, 56, 59, 6, 63, 57, 62, 11, 36, 20, 34, 44, 52,
};

/* the 32 characters in front of the extension of the last path segment */
static int image_key(const char *url, char out[32])
{
	const char *name = strrchr(url, '/');
	const char *dot;

	name = name != NULL ? name + 1 : url;
	dot = strchr(name, '.');
	if (dot == NULL || dot - name != 32) {
		return -EINVAL;
	}
	memcpy(out, name, 32);

	return 0;
}

int wbi_make_key(const char *img_url, const char *sub_url, char key[33])
{
	char raw[64];

	if (image_key(img_url, raw) != 0 || image_key(sub_url, raw + 32) != 0) {
		return -EINVAL;
	}
	for (int i = 0; i < 32; i++) {
		key[i] = raw[mixin[i]];
	}
	key[32] = '\0';

	return 0;
}

/* order by name: the text up to the '=' */
static int name_cmp(const char *a, const char *b)
{
	for (;; a++, b++) {
		unsigned char x = *a == '=' ? 0 : *a, y = *b == '=' ? 0 : *b;

		if (x != y || x == 0) {
			return (int)x - (int)y;
		}
	}
}

/* percent encoding of everything but the unreserved characters, after the server's filter */
static int encode(const char *s, char *out, size_t size)
{
	size_t n = 0;

	for (; *s != '\0'; s++) {
		unsigned char c = *s;

		if (strchr("!'()*", c) != NULL) {
			continue;
		}
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    c == '-' || c == '_' || c == '.' || c == '~') {
			if (n + 1 >= size) {
				return -ENOSPC;
			}
			out[n++] = c;
		} else {
			if (n + 3 >= size) {
				return -ENOSPC;
			}
			n += snprintf(out + n, size - n, "%%%02X", c);
		}
	}
	out[n] = '\0';

	return n;
}

int wbi_query(const char *const *params, int count, const char *key, int64_t wts, char *out,
	      size_t size)
{
	char wts_pair[32];
	const char *all[MAX_PARAMS];
	size_t n = 0;
	char hash[33];
	int total = count + 1;

	if (total > MAX_PARAMS) {
		return -EINVAL;
	}
	snprintf(wts_pair, sizeof(wts_pair), "wts=%lld", (long long)wts);
	for (int i = 0; i < count; i++) {
		all[i] = params[i];
	}
	all[count] = wts_pair;

	/* sort by name (insertion sort, the names are short and the list is too) */
	for (int i = 1; i < total; i++) {
		const char *cur = all[i];
		int j = i - 1;

		while (j >= 0 && name_cmp(all[j], cur) > 0) {
			all[j + 1] = all[j];
			j--;
		}
		all[j + 1] = cur;
	}
	for (int i = 0; i < total; i++) {
		const char *eq = strchr(all[i], '=');
		int ret;

		if (eq == NULL || n + 2 >= size) {
			return -EINVAL;
		}
		if (n > 0) {
			out[n++] = '&';
		}
		if ((size_t)(eq - all[i]) + 1 >= size - n) {
			return -ENOSPC;
		}
		memcpy(out + n, all[i], eq - all[i] + 1);
		n += eq - all[i] + 1;
		ret = encode(eq + 1, out + n, size - n);
		if (ret < 0) {
			return ret;
		}
		n += ret;
	}
	{
		size_t klen = strlen(key);
		char tmp[1024];

		if (n + klen >= sizeof(tmp)) {
			return -ENOSPC;
		}
		memcpy(tmp, out, n);
		memcpy(tmp + n, key, klen);
		md5_hex(tmp, n + klen, hash);
	}
	if (n + 8 + 32 >= size) {
		return -ENOSPC;
	}
	n += snprintf(out + n, size - n, "&w_rid=%s", hash);

	return n;
}

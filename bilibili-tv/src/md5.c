/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "md5.h"

static const uint32_t K[64] = {
	0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
	0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
	0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
	0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
	0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
	0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
	0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
	0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};
static const uint8_t S[64] = {
	7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
	5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
	4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
	6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

static void block(uint32_t h[4], const uint8_t *p)
{
	uint32_t m[16], a = h[0], b = h[1], c = h[2], d = h[3];

	for (int i = 0; i < 16; i++) {
		m[i] = (uint32_t)p[4 * i] | (uint32_t)p[4 * i + 1] << 8 | (uint32_t)p[4 * i + 2] << 16 |
		       (uint32_t)p[4 * i + 3] << 24;
	}
	for (int i = 0; i < 64; i++) {
		uint32_t f, t;
		int g;

		if (i < 16) {
			f = (b & c) | (~b & d);
			g = i;
		} else if (i < 32) {
			f = (d & b) | (~d & c);
			g = (5 * i + 1) & 15;
		} else if (i < 48) {
			f = b ^ c ^ d;
			g = (3 * i + 5) & 15;
		} else {
			f = c ^ (b | ~d);
			g = (7 * i) & 15;
		}
		t = d;
		d = c;
		c = b;
		f += a + K[i] + m[g];
		b += (f << S[i]) | (f >> (32 - S[i]));
		a = t;
	}
	h[0] += a;
	h[1] += b;
	h[2] += c;
	h[3] += d;
}

void md5_hex(const void *data, size_t len, char out[33])
{
	uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
	const uint8_t *p = data;
	uint8_t tail[128];
	size_t rest = len % 64, tail_len = rest < 56 ? 64 : 128;
	uint64_t bits = (uint64_t)len * 8;

	for (size_t i = 0; i + 64 <= len; i += 64) {
		block(h, p + i);
	}
	memset(tail, 0, sizeof(tail));
	memcpy(tail, p + len - rest, rest);
	tail[rest] = 0x80;
	for (int i = 0; i < 8; i++) {
		tail[tail_len - 8 + i] = bits >> (8 * i);
	}
	for (size_t i = 0; i < tail_len; i += 64) {
		block(h, tail + i);
	}
	for (int i = 0; i < 16; i++) {
		static const char hex[] = "0123456789abcdef";
		uint8_t byte = h[i / 4] >> (8 * (i % 4));

		out[2 * i] = hex[byte >> 4];
		out[2 * i + 1] = hex[byte & 15];
	}
	out[32] = '\0';
}

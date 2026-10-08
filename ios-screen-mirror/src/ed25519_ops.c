/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Field arithmetic mod 2^255 - 19 on 16 limbs of 16 bits (the layout and the formulas of
 * TweetNaCl, public domain), the unified addition law of the twisted Edwards curve and a
 * constant time ladder. The curve constants are computed at first use from their definitions.
 */

#include <stdbool.h>
#include <string.h>
#include "ed25519_ops.h"

typedef int64_t i64;
typedef uint64_t u64;
typedef uint8_t u8;
typedef ed_gf gf;

#define FOR(i, n) for (i = 0; i < n; i++)

static const gf gf0;
static const gf gf1 = {1};

static gf D, D2, I_SQRT, BX, BY;
static bool ready;

static void set25519(gf r, const gf a)
{
	int i;

	FOR(i, 16) r[i] = a[i];
}

static void car25519(gf o)
{
	int i;
	i64 c;

	FOR(i, 16) {
		o[i] += (1LL << 16);
		c = o[i] >> 16;
		o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
		o[i] -= c << 16;
	}
}

static void sel25519(gf p, gf q, int b)
{
	i64 t, i, c = ~(b - 1);

	FOR(i, 16) {
		t = c & (p[i] ^ q[i]);
		p[i] ^= t;
		q[i] ^= t;
	}
}

static void pack25519(u8 *o, const gf n)
{
	int i, j, b;
	gf m, t;

	FOR(i, 16) t[i] = n[i];
	car25519(t);
	car25519(t);
	car25519(t);
	FOR(j, 2) {
		m[0] = t[0] - 0xffed;
		for (i = 1; i < 15; i++) {
			m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
			m[i - 1] &= 0xffff;
		}
		m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
		b = (m[15] >> 16) & 1;
		m[14] &= 0xffff;
		sel25519(t, m, 1 - b);
	}
	FOR(i, 16) {
		o[2 * i] = t[i] & 0xff;
		o[2 * i + 1] = t[i] >> 8;
	}
}

static int neq25519(const gf a, const gf b)
{
	u8 c[32], d[32];
	u8 x = 0;
	int i;

	pack25519(c, a);
	pack25519(d, b);
	FOR(i, 32) x |= c[i] ^ d[i];

	return x != 0;
}

static u8 par25519(const gf a)
{
	u8 d[32];

	pack25519(d, a);

	return d[0] & 1;
}

static void unpack25519(gf o, const u8 *n)
{
	int i;

	FOR(i, 16) o[i] = n[2 * i] + ((i64)n[2 * i + 1] << 8);
	o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b)
{
	int i;

	FOR(i, 16) o[i] = a[i] + b[i];
}

static void Z(gf o, const gf a, const gf b)
{
	int i;

	FOR(i, 16) o[i] = a[i] - b[i];
}

static void M(gf o, const gf a, const gf b)
{
	i64 i, j, t[31];

	FOR(i, 31) t[i] = 0;
	FOR(i, 16) FOR(j, 16) t[i + j] += a[i] * b[j];
	FOR(i, 15) t[i] += 38 * t[i + 16];
	FOR(i, 16) o[i] = t[i];
	car25519(o);
	car25519(o);
}

static void S(gf o, const gf a)
{
	M(o, a, a);
}

static void inv25519(gf o, const gf i)
{
	gf c;
	int a;

	FOR(a, 16) c[a] = i[a];
	for (a = 253; a >= 0; a--) {
		S(c, c);
		if (a != 2 && a != 4) {
			M(c, c, i);
		}
	}
	FOR(a, 16) o[a] = c[a];
}

static void pow2523(gf o, const gf i)
{
	gf c;
	int a;

	FOR(a, 16) c[a] = i[a];
	for (a = 250; a >= 0; a--) {
		S(c, c);
		if (a != 1) {
			M(c, c, i);
		}
	}
	FOR(a, 16) o[a] = c[a];
}

static void gf_small(gf r, int64_t v)
{
	memset(r, 0, sizeof(gf));
	r[0] = v;
}

static void add(gf p[4], gf q[4])
{
	gf a, b, c, d, t, e, f, g, h;

	Z(a, p[1], p[0]);
	Z(t, q[1], q[0]);
	M(a, a, t);
	A(b, p[0], p[1]);
	A(t, q[0], q[1]);
	M(b, b, t);
	M(c, p[3], q[3]);
	M(c, c, D2);
	M(d, p[2], q[2]);
	A(d, d, d);
	Z(e, b, a);
	Z(f, d, c);
	A(g, d, c);
	A(h, b, a);
	M(p[0], e, f);
	M(p[1], h, g);
	M(p[2], g, f);
	M(p[3], e, h);
}

static void cswap(gf p[4], gf q[4], u8 b)
{
	int i;

	FOR(i, 4) sel25519(p[i], q[i], b);
}

/* x of the point with the given y and sign, negated as TweetNaCl does; -1 when none exists */
static int unpackneg(gf r[4], const u8 p[32])
{
	gf t, chk, num, den, den2, den4, den6;

	set25519(r[2], gf1);
	unpack25519(r[1], p);
	S(num, r[1]);
	M(den, num, D);
	Z(num, num, r[2]);
	A(den, r[2], den);

	S(den2, den);
	S(den4, den2);
	M(den6, den4, den2);
	M(t, den6, num);
	M(t, t, den);

	pow2523(t, t);
	M(t, t, num);
	M(t, t, den);
	M(t, t, den);
	M(r[0], t, den);

	S(chk, r[0]);
	M(chk, chk, den);
	if (neq25519(chk, num)) {
		M(r[0], r[0], I_SQRT);
	}
	S(chk, r[0]);
	M(chk, chk, den);
	if (neq25519(chk, num)) {
		return -1;
	}
	if (par25519(r[0]) == (p[31] >> 7)) {
		Z(r[0], gf0, r[0]);
	}
	M(r[3], r[0], r[1]);

	return 0;
}

static void init(void)
{
	gf t, u;
	u8 by[32];
	gf q[4];
	int i;

	if (ready) {
		return;
	}
	/* d = -121665 / 121666 */
	gf_small(t, 121665);
	gf_small(u, 121666);
	inv25519(u, u);
	M(t, t, u);
	Z(D, gf0, t);
	A(D2, D, D);
	/* sqrt(-1) = 2^((p - 1) / 4), 2 is not a square mod p */
	{
		gf two, c;

		gf_small(two, 2);
		/* 2^((p-1)/4): p - 1 = 2^255 - 20, /4 = 2^253 - 5 */
		set25519(c, two);
		/* 2^253 - 5 is 253 one bits except bit 2; the top bit is the start value */
		for (i = 251; i >= 0; i--) {
			S(c, c);
			if (i != 2) {
				M(c, c, two);
			}
		}
		set25519(I_SQRT, c);
	}
	/* B: y = 4/5, x even */
	gf_small(t, 4);
	gf_small(u, 5);
	inv25519(u, u);
	M(BY, t, u);
	pack25519(by, BY);
	if (unpackneg(q, by) == 0) {
		Z(BX, gf0, q[0]);
	}
	ready = true;
}

static void point_set(gf p[4], const gf x, const gf y)
{
	set25519(p[0], x);
	set25519(p[1], y);
	set25519(p[2], gf1);
	M(p[3], x, y);
}

int ed_decode(ed_point r, const uint8_t in[32])
{
	init();
	if (unpackneg(r, in) != 0) {
		return -1;
	}
	Z(r[0], gf0, r[0]);
	Z(r[3], gf0, r[3]);

	return 0;
}

void ed_encode(uint8_t out[32], const ed_point p)
{
	gf tx, ty, zi;

	inv25519(zi, p[2]);
	M(tx, p[0], zi);
	M(ty, p[1], zi);
	pack25519(out, ty);
	out[31] ^= par25519(tx) << 7;
}

void ed_scalarmult(ed_point r, const ed_point pt, const uint8_t s[32])
{
	gf q[4];
	int i;

	init();
	FOR(i, 4) set25519(q[i], pt[i]);
	set25519(r[0], gf0);
	set25519(r[1], gf1);
	set25519(r[2], gf1);
	set25519(r[3], gf0);
	for (i = 255; i >= 0; --i) {
		u8 b = (s[i / 8] >> (i & 7)) & 1;

		cswap(r, q, b);
		add(q, r);
		add(r, r);
		cswap(r, q, b);
	}
}

void ed_base_mult(ed_point r, const uint8_t s[32])
{
	gf b[4];

	init();
	point_set(b, BX, BY);
	ed_scalarmult(r, b, s);
}

void ed_add(ed_point p, const ed_point q)
{
	gf t[4];
	int i;

	init();
	FOR(i, 4) set25519(t[i], q[i]);
	add(p, t);
}

void ed_neg(ed_point p)
{
	Z(p[0], gf0, p[0]);
	Z(p[3], gf0, p[3]);
}

void ed_mul8(ed_point p)
{
	init();
	add(p, p);
	add(p, p);
	add(p, p);
}

static const u64 L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
			  0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
			  0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

static void modL(u8 *r, i64 x[64])
{
	i64 carry, i, j;

	for (i = 63; i >= 32; --i) {
		carry = 0;
		for (j = i - 32; j < i - 12; ++j) {
			x[j] += carry - 16 * x[i] * (i64)L[j - (i - 32)];
			carry = (x[j] + 128) >> 8;
			x[j] -= carry << 8;
		}
		x[j] += carry;
		x[i] = 0;
	}
	carry = 0;
	FOR(j, 32) {
		x[j] += carry - (x[31] >> 4) * (i64)L[j];
		carry = x[j] >> 8;
		x[j] &= 255;
	}
	FOR(j, 32) x[j] -= carry * (i64)L[j];
	FOR(i, 32) {
		x[i + 1] += x[i] >> 8;
		r[i] = x[i] & 255;
	}
}

void ed_scalar_reduce64(uint8_t out[32], const uint8_t in[64])
{
	i64 x[64];
	u8 r[64];
	int i;

	FOR(i, 64) x[i] = in[i];
	modL(r, x);
	memcpy(out, r, 32);
}

void ed_scalar_muladd(uint8_t out[32], const uint8_t h[32], const uint8_t a[32],
		      const uint8_t r[32])
{
	i64 x[64] = {0};
	u8 o[64];
	int i, j;

	FOR(i, 32) x[i] = r[i];
	FOR(i, 32) FOR(j, 32) x[i + j] += (i64)h[i] * a[j];
	modL(o, x);
	memcpy(out, o, 32);
}

void ed_x25519(uint8_t out[32], const uint8_t n[32], const uint8_t p[32])
{
	static const gf c121665 = {0xdb41, 1};
	u8 z[32];
	i64 r;
	gf x, a, b, c, d, e, f;
	int i;

	memcpy(z, n, 32);
	z[31] = (n[31] & 127) | 64;
	z[0] &= 248;
	unpack25519(x, p);
	FOR(i, 16) {
		b[i] = x[i];
		d[i] = a[i] = c[i] = 0;
	}
	a[0] = d[0] = 1;
	for (i = 254; i >= 0; --i) {
		r = (z[i >> 3] >> (i & 7)) & 1;
		sel25519(a, b, r);
		sel25519(c, d, r);
		A(e, a, c);
		Z(a, a, c);
		A(c, b, d);
		Z(b, b, d);
		S(d, e);
		S(f, a);
		M(a, c, a);
		M(c, b, e);
		A(e, a, c);
		Z(a, a, c);
		S(b, a);
		Z(c, d, f);
		M(a, c, c121665);
		A(a, a, d);
		M(c, c, a);
		M(a, d, f);
		M(d, b, x);
		S(b, e);
		sel25519(a, b, r);
		sel25519(c, d, r);
	}
	inv25519(c, c);
	M(a, a, c);
	pack25519(out, a);
}

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* Arithmetic on the Edwards curve of Ed25519, as far as the SPAKE2 pairing needs it */

#ifndef ED25519_OPS_H_
#define ED25519_OPS_H_

#include <stdint.h>

typedef int64_t ed_gf[16];
/** A point in extended coordinates */
typedef ed_gf ed_point[4];

/** Decode a compressed point; 0 or -1 when it is not on the curve */
int ed_decode(ed_point r, const uint8_t in[32]);
void ed_encode(uint8_t out[32], const ed_point p);
/** r = s * p (s: 32 bytes, little endian, any value below 2^256) */
void ed_scalarmult(ed_point r, const ed_point p, const uint8_t s[32]);
/** r = s * B */
void ed_base_mult(ed_point r, const uint8_t s[32]);
/** p = p + q */
void ed_add(ed_point p, const ed_point q);
/** p = -p */
void ed_neg(ed_point p);
/** p = 8 * p */
void ed_mul8(ed_point p);
/** 64 byte little endian number to its remainder mod the group order (32 bytes) */
void ed_scalar_reduce64(uint8_t out[32], const uint8_t in[64]);

#endif

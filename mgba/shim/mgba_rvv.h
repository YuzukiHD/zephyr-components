/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Forced into every source of the emulator library: copies, fills and compares that
 * are not tiny go through the vector unit. A size known at compile time and small
 * stays a plain builtin, the compiler turns that into a few moves.
 */

#ifndef MGBA_RVV_H_
#define MGBA_RVV_H_

#include <string.h>

#if defined(__riscv_v) && !defined(MGBA_NO_RVV)
#include <riscv_vector.h>

static inline void *mgba_rvv_memcpy(void *restrict dst, const void *restrict src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;

	while (n > 0) {
		size_t vl = __riscv_vsetvl_e8m8(n);

		__riscv_vse8_v_u8m8(d, __riscv_vle8_v_u8m8(s, vl), vl);
		d += vl;
		s += vl;
		n -= vl;
	}

	return dst;
}

static inline void *mgba_rvv_memset(void *dst, int c, size_t n)
{
	uint8_t *d = dst;

	while (n > 0) {
		size_t vl = __riscv_vsetvl_e8m8(n);

		__riscv_vse8_v_u8m8(d, __riscv_vmv_v_x_u8m8((uint8_t)c, vl), vl);
		d += vl;
		n -= vl;
	}

	return dst;
}

/* the same result as the library function: the difference of the first bytes that differ */
static inline int mgba_rvv_memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *p = a;
	const uint8_t *q = b;

	while (n > 0) {
		size_t vl = __riscv_vsetvl_e8m8(n);
		vbool1_t ne = __riscv_vmsne_vv_u8m8_b1(__riscv_vle8_v_u8m8(p, vl),
						       __riscv_vle8_v_u8m8(q, vl), vl);
		long i = __riscv_vfirst_m_b1(ne, vl);

		if (i >= 0) {
			return (int)p[i] - (int)q[i];
		}
		p += vl;
		q += vl;
		n -= vl;
	}

	return 0;
}

#define MGBA_SMALL(n) (__builtin_constant_p(n) && (n) <= 64)

#define memcpy(d, s, n) (MGBA_SMALL(n) ? __builtin_memcpy(d, s, n) : mgba_rvv_memcpy(d, s, n))
#define memset(d, c, n) (MGBA_SMALL(n) ? __builtin_memset(d, c, n) : mgba_rvv_memset(d, c, n))
#define memcmp(a, b, n) mgba_rvv_memcmp(a, b, n)
#endif

#endif /* MGBA_RVV_H_ */

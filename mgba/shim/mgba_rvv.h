/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Forced into every source of the emulator library: copies and fills that are
 * not tiny go through the vector unit. A size known at compile time and small
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

#define MGBA_SMALL(n) (__builtin_constant_p(n) && (n) <= 64)

#define memcpy(d, s, n) (MGBA_SMALL(n) ? __builtin_memcpy(d, s, n) : mgba_rvv_memcpy(d, s, n))
#define memset(d, c, n) (MGBA_SMALL(n) ? __builtin_memset(d, c, n) : mgba_rvv_memset(d, c, n))
#endif

#endif /* MGBA_RVV_H_ */

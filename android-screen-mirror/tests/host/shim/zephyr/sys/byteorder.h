/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_ZEPHYR_BYTEORDER_H_
#define TEST_ZEPHYR_BYTEORDER_H_

#include <stdint.h>

static inline void sys_put_be32(uint32_t value, uint8_t *dst)
{
	dst[0] = value >> 24;
	dst[1] = value >> 16;
	dst[2] = value >> 8;
	dst[3] = value;
}

static inline uint32_t sys_get_be32(const uint8_t *src)
{
	return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
	       ((uint32_t)src[2] << 8) | src[3];
}

#endif

/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <endian.h>
static inline uint16_t sys_get_be16(const uint8_t *p)
{
	return (uint16_t)p[0] << 8 | p[1];
}
static inline uint32_t sys_get_be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline uint64_t sys_get_be64(const uint8_t *p)
{
	return (uint64_t)sys_get_be32(p) << 32 | sys_get_be32(p + 4);
}
#define sys_be32_to_cpu(v) be32toh(v)

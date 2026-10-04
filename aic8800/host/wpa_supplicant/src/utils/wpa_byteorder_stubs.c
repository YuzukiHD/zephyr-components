/*
 * External function implementations for WPA byte-order helpers.
 *
 * Some RISC-V toolchains (Xuantie GCC 14.3.0) emit external references for
 * static inline functions defined in common.h under certain optimization
 * patterns. These stubs satisfy those linker references at global scope
 * without conflicting with the internal-linkage static inline versions.
 */
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

u32 WPA_GET_BE32(const u8 *a)
{
	return ((u32) a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
}

void WPA_PUT_BE32(u8 *a, u32 val)
{
	a[0] = (u8)(val >> 24);
	a[1] = (u8)(val >> 16);
	a[2] = (u8)(val >> 8);
	a[3] = (u8)(val & 0xff);
}

void WPA_PUT_BE64(u8 *a, u64 val)
{
	a[0] = (u8)(val >> 56);
	a[1] = (u8)(val >> 48);
	a[2] = (u8)(val >> 40);
	a[3] = (u8)(val >> 32);
	a[4] = (u8)(val >> 24);
	a[5] = (u8)(val >> 16);
	a[6] = (u8)(val >> 8);
	a[7] = (u8)(val & 0xff);
}

void WPA_PUT_LE16(u8 *a, u16 val)
{
	a[1] = (u8)(val >> 8);
	a[0] = (u8)(val & 0xff);
}

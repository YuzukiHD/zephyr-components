/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define UF2_MAGIC_START0 0x0A324655U /* "UF2\n" */
#define UF2_MAGIC_START1 0x9E5D5157U
#define UF2_MAGIC_END	 0x0AB16F30U

#define UF2_FLAG_NOT_MAIN_FLASH 0x00000001U
#define UF2_FLAG_FAMILY_ID	0x00002000U

/* applications: flash offsets from APP_BASE on */
#define UF2_FAMILY_APP 0x31303146U
/* everything (loader, this program): the same file format, offsets from 0 */
#define UF2_FAMILY_SYS 0x31303147U

#define UF2_APP_BASE 0x00200000U

struct uf2_block {
	uint32_t magic0, magic1;
	uint32_t flags;
	uint32_t target_addr;
	uint32_t payload_size;
	uint32_t block_no;
	uint32_t num_blocks;
	uint32_t family;
	uint8_t data[476];
	uint32_t magic_end;
};

/** Initialise; returns 0 or a negative errno when the flash is not usable. */
int uf2_init(void);

/**
 * Handle a 512 byte block written to the volume. Returns false when it is not a UF2 block
 * (a file system write), true when it was one, whatever came of it.
 */
bool uf2_write_block(const uint8_t *buf);

/** The state for the console: blocks written, blocks of the file, errors. */
uint32_t uf2_blocks_done(void);
uint32_t uf2_blocks_total(void);
uint32_t uf2_errors(void);

/** Set once every block of a file has been written. */
bool uf2_complete(void);

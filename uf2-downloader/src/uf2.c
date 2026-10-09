/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* UF2 blocks -> SPI NOR flash */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "uf2.h"

#define SECTOR 4096U

static const struct device *const flash = DEVICE_DT_GET(DT_NODELABEL(spif));
static uint32_t flash_size;
/* 4 KiB sectors erased for the file in progress */
static uint8_t erased[4096 / 8];
static uint32_t done, total, errors;
static bool complete;
static uint32_t file_family;

int uf2_init(void)
{
	uint64_t size;

	if (!device_is_ready(flash)) {
		return -ENODEV;
	}
	if (flash_get_size(flash, &size) != 0 || size == 0U) {
		return -EIO;
	}
	flash_size = size;
	return 0;
}

static bool allowed(uint32_t family, uint32_t addr, uint32_t len)
{
	if (len == 0U || addr + len < addr || addr + len > flash_size) {
		return false;
	}
	if (family == UF2_FAMILY_APP) {
		return addr >= UF2_APP_BASE;
	}
	return family == UF2_FAMILY_SYS;
}

static int prepare(uint32_t addr, uint32_t len)
{
	for (uint32_t s = addr / SECTOR; s <= (addr + len - 1U) / SECTOR; s++) {
		if (erased[s / 8U] & BIT(s % 8U)) {
			continue;
		}
		int ret = flash_erase(flash, s * SECTOR, SECTOR);

		if (ret != 0) {
			return ret;
		}
		erased[s / 8U] |= BIT(s % 8U);
	}
	return 0;
}

bool uf2_write_block(const uint8_t *buf)
{
	const struct uf2_block *b = (const struct uf2_block *)buf;

	if (b->magic0 != UF2_MAGIC_START0 || b->magic1 != UF2_MAGIC_START1 ||
	    b->magic_end != UF2_MAGIC_END) {
		return false;
	}
	if ((b->flags & UF2_FLAG_NOT_MAIN_FLASH) != 0U) {
		return true;
	}
	if ((b->flags & UF2_FLAG_FAMILY_ID) == 0U || b->payload_size > sizeof(b->data) ||
	    !allowed(b->family, b->target_addr, b->payload_size)) {
		errors++;
		printk("uf2: block %u refused (family 0x%08x, address 0x%08x, %u bytes)\n",
		       b->block_no, b->family, b->target_addr, b->payload_size);
		return true;
	}
	if (b->block_no == 0U || b->family != file_family || b->num_blocks != total) {
		/* the first block of a new file */
		memset(erased, 0, sizeof(erased));
		done = 0;
		errors = 0;
		complete = false;
		total = b->num_blocks;
		file_family = b->family;
		printk("uf2: new file, %u blocks, family 0x%08x\n", total, file_family);
	}
	if (prepare(b->target_addr, b->payload_size) != 0 ||
	    flash_write(flash, b->target_addr, b->data, b->payload_size) != 0) {
		errors++;
		printk("uf2: flash error at 0x%08x\n", b->target_addr);
		return true;
	}
	if (++done >= total && errors == 0U) {
		complete = true;
		printk("uf2: %u blocks written\n", done);
	}
	return true;
}

uint32_t uf2_blocks_done(void)
{
	return done;
}

uint32_t uf2_blocks_total(void)
{
	return total;
}

uint32_t uf2_errors(void)
{
	return errors;
}

bool uf2_complete(void)
{
	return complete;
}

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A FAT16 volume that exists only as the code below: the boot sector, the tables, the root
 * directory and two small files are made when read, everything written is looked at for
 * UF2 blocks and dropped otherwise. 128 MiB, so a big UF2 file is not refused for space.
 */

#include <string.h>

#include "usbd_core.h"
#include "usbd_msc.h"

#include "uf2.h"

#define SECTOR_SIZE	512U
#define NUM_SECTORS	262144U
#define SEC_PER_CLUSTER 8U
#define FAT_SECTORS	128U /* 32768 clusters * 2 bytes */
#define ROOT_ENTRIES	512U
#define ROOT_SECTORS	(ROOT_ENTRIES * 32U / SECTOR_SIZE)
#define FAT1_START	1U
#define FAT2_START	(FAT1_START + FAT_SECTORS)
#define ROOT_START	(FAT2_START + FAT_SECTORS)
#define DATA_START	(ROOT_START + ROOT_SECTORS)

static const char info_txt[] =
	"UF2 Bootloader for the Allwinner F101\r\n"
	"Model: YuzukiNeko\r\n"
	"Board-ID: F101-Neko\r\n"
	"Flash: applications from 0x200000, family 0x31303146\r\n";

static const char index_htm[] =
	"<!doctype html><html><body><script>location.replace(\"https://github.com/YuzukiHD\");"
	"</script></body></html>\r\n";

struct vfile {
	const char name[11];
	const char *data;
	uint16_t size;
};

static const struct vfile files[] = {
	{"INFO_UF2TXT", info_txt, sizeof(info_txt) - 1},
	{"INDEX   HTM", index_htm, sizeof(index_htm) - 1},
};

#define FIRST_FILE_CLUSTER 2U

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = v;
	p[1] = v >> 8;
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

static void boot_sector(uint8_t *b)
{
	memset(b, 0, SECTOR_SIZE);
	b[0] = 0xEB;
	b[1] = 0x3C;
	b[2] = 0x90;
	memcpy(b + 3, "UF2 UF2 ", 8);
	put16(b + 11, SECTOR_SIZE);
	b[13] = SEC_PER_CLUSTER;
	put16(b + 14, 1);
	b[16] = 2;
	put16(b + 17, ROOT_ENTRIES);
	put16(b + 19, 0);
	b[21] = 0xF8;
	put16(b + 22, FAT_SECTORS);
	put16(b + 24, 63);
	put16(b + 26, 255);
	put32(b + 28, 0);
	put32(b + 32, NUM_SECTORS);
	b[36] = 0x80;
	b[38] = 0x29;
	put32(b + 39, 0x00420042);
	memcpy(b + 43, "F101BOOT   ", 11);
	memcpy(b + 54, "FAT16   ", 8);
	b[510] = 0x55;
	b[511] = 0xAA;
}

static uint16_t fat_entry(uint32_t idx)
{
	if (idx == 0) {
		return 0xFFF8;
	}
	if (idx == 1) {
		return 0xFFFF;
	}
	if (idx >= FIRST_FILE_CLUSTER && idx < FIRST_FILE_CLUSTER + ARRAY_SIZE(files)) {
		return 0xFFFF; /* every file is one cluster */
	}
	return 0;
}

static void root_sector(uint32_t n, uint8_t *b)
{
	memset(b, 0, SECTOR_SIZE);
	if (n != 0U) {
		return;
	}
	/* volume label, then the files; 1 Jan 2026 */
	memcpy(b, "F101BOOT   ", 11);
	b[11] = 0x08;
	for (unsigned int i = 0; i < ARRAY_SIZE(files); i++) {
		uint8_t *e = b + 32 * (i + 1);

		memcpy(e, files[i].name, 11);
		e[11] = 0x01; /* read only */
		put16(e + 14, 0);
		put16(e + 16, ((2026 - 1980) << 9) | (1 << 5) | 1);
		put16(e + 24, ((2026 - 1980) << 9) | (1 << 5) | 1);
		put16(e + 26, FIRST_FILE_CLUSTER + i);
		put32(e + 28, files[i].size);
	}
}

static void read_sector(uint32_t s, uint8_t *b)
{
	if (s == 0U) {
		boot_sector(b);
	} else if (s < ROOT_START) {
		uint32_t n = (s - FAT1_START) % FAT_SECTORS;

		for (unsigned int i = 0; i < SECTOR_SIZE / 2U; i++) {
			put16(b + 2U * i, fat_entry(n * (SECTOR_SIZE / 2U) + i));
		}
	} else if (s < DATA_START) {
		root_sector(s - ROOT_START, b);
	} else {
		uint32_t cluster = FIRST_FILE_CLUSTER + (s - DATA_START) / SEC_PER_CLUSTER;
		uint32_t in_cluster = (s - DATA_START) % SEC_PER_CLUSTER;

		memset(b, 0, SECTOR_SIZE);
		if (cluster < FIRST_FILE_CLUSTER + ARRAY_SIZE(files) && in_cluster == 0U) {
			const struct vfile *f = &files[cluster - FIRST_FILE_CLUSTER];

			memcpy(b, f->data, f->size);
		}
	}
}

void usbd_msc_get_cap(uint8_t busid, uint8_t lun, uint32_t *block_num, uint32_t *block_size)
{
	*block_num = NUM_SECTORS;
	*block_size = SECTOR_SIZE;
}

int usbd_msc_sector_read(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer,
			 uint32_t length)
{
	for (uint32_t i = 0; i < length / SECTOR_SIZE; i++) {
		read_sector(sector + i, buffer + i * SECTOR_SIZE);
	}
	return 0;
}

int usbd_msc_sector_write(uint8_t busid, uint8_t lun, uint32_t sector, uint8_t *buffer,
			  uint32_t length)
{
	for (uint32_t i = 0; i < length / SECTOR_SIZE; i++) {
		(void)uf2_write_block(buffer + i * SECTOR_SIZE);
	}
	return 0;
}

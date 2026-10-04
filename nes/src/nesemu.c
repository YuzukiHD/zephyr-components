/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <nes_zephyr/nesemu.h>

#include "nes.h"
#include "palettes.h"

#define SAMPLE_FRAMES_MAX 1024

static uint8_t vidbuf[NES_SCREEN_PITCH * NES_SCREEN_HEIGHT];
static uint16_t palette[256];
static uint16_t *out_buf;
static int skip_n, skip_left;
static int drawn;
static bool open_;

static int16_t pcm[SAMPLE_FRAMES_MAX * 2];
static size_t pcm_frames, pcm_pos;

static char save_file[128];
static uint32_t save_crc;

uint32_t nes_crc32(const void *data, size_t len)
{
	return crc32_ieee(data, len);
}

static void palette_build(void)
{
	const uint8_t *p = nes_palettes[0];

	for (int i = 0; i < 256; i++) {
		const uint8_t *rgb = &p[(i & 63) * 3];

		palette[i] = (rgb[2] >> 3) | ((rgb[1] >> 2) << 5) | ((rgb[0] >> 3) << 11);
	}
}

/* the 8 bit picture of the PPU to RGB565, without the overscan lines */
static void blit(uint8 *src)
{
	nes_t *nes = nes_getptr();
	unsigned int h = nesemu_height();

	if (out_buf == NULL) {
		return;
	}
	src = NES_SCREEN_GETPTR(src, 0, nes->overscan);
	for (unsigned int y = 0; y < h; y++) {
		uint16_t *d = out_buf + y * NESEMU_WIDTH;

		for (int x = 0; x < NESEMU_WIDTH; x++) {
			d[x] = palette[src[x]];
		}
		src += NES_SCREEN_PITCH;
	}
	drawn = 1;
}

static int read_file(const char *path, uint8_t **data, size_t *size)
{
	struct fs_dirent ent;
	struct fs_file_t f;
	uint8_t *buf;
	int ret;

	ret = fs_stat(path, &ent);
	if (ret != 0) {
		return ret;
	}
	if (ent.size < 16 || ent.size > CONFIG_NES_MAX_ROM_SIZE_KB * 1024UL) {
		return -EFBIG;
	}
	buf = malloc(ent.size);
	if (buf == NULL) {
		return -ENOMEM;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret != 0) {
		free(buf);
		return ret;
	}
	ret = fs_read(&f, buf, ent.size);
	fs_close(&f);
	if (ret != (int)ent.size) {
		free(buf);
		return ret < 0 ? ret : -EIO;
	}
	*data = buf;
	*size = ent.size;

	return 0;
}

static void save_load(rom_t *cart)
{
	struct fs_file_t f;
	size_t len = cart->prg_ram_banks * ROM_PRG_BANK_SIZE;

	fs_file_t_init(&f);
	if (fs_open(&f, save_file, FS_O_READ) == 0) {
		fs_read(&f, cart->prg_ram, len);
		fs_close(&f);
	}
	save_crc = crc32_ieee(cart->prg_ram, len);
}

int nesemu_open(const char *rom_path, const char *save_path, unsigned int out_rate)
{
	uint8_t *data;
	size_t size;
	nes_t *nes;
	rom_t *cart;
	int ret;

	if (open_) {
		nesemu_close();
	}
	ret = read_file(rom_path, &data, &size);
	if (ret != 0) {
		return ret;
	}
	nes = nes_init(SYS_DETECT, out_rate, false, NULL);
	if (nes == NULL) {
		free(data);
		return -ENOMEM;
	}
	cart = rom_loadmem(data, size);
	if (cart == NULL) {
		free(data);
		nes_shutdown();
		return -EINVAL;
	}
	cart->free_data_ptr = true;
	if (strstr(rom_path, "(E)") != NULL || strstr(rom_path, "(Europe)") != NULL) {
		cart->system = SYS_NES_PAL;
	}
	ret = nes_insertcart(cart);
	if (ret != 0) {
		return -ENOTSUP;
	}
	nes->blit_func = blit;
	nes_setvidbuf(vidbuf);
	palette_build();

	save_file[0] = '\0';
	if (save_path != NULL && cart->battery) {
		strncpy(save_file, save_path, sizeof(save_file) - 1);
		save_file[sizeof(save_file) - 1] = '\0';
		save_load(cart);
	}
	pcm_frames = pcm_pos = 0;
	skip_left = 0;
	open_ = true;

	return 0;
}

void nesemu_close(void)
{
	if (!open_) {
		return;
	}
	nesemu_save_flush();
	nes_shutdown();
	open_ = false;
}

unsigned int nesemu_height(void)
{
	return NES_SCREEN_HEIGHT - 2 * nes_getptr()->overscan;
}

unsigned int nesemu_fps_x1000(void)
{
	return nes_getptr()->refresh_rate == 50 ? 50007 : 60099;
}

void nesemu_set_frame_buffer(uint16_t *buf)
{
	out_buf = buf;
}

void nesemu_set_frameskip(int skip)
{
	skip_n = skip;
}

void nesemu_set_keys(uint32_t keys)
{
	/* the key bits of the API are the pad bits of the core, up to the order of the directions */
	input_update(0, keys & 0xff);
}

void nesemu_reset(void)
{
	if (open_) {
		nes_reset(false);
		nes_setvidbuf(vidbuf);
	}
}

int nesemu_run_frame(void)
{
	nes_t *nes = nes_getptr();
	apu_t *apu = nes->apu;
	bool draw = skip_left == 0;
	int n;

	skip_left = draw ? skip_n : skip_left - 1;
	drawn = 0;
	nes_emulate(draw);

	/* the sound of the frame, mono to stereo; what the caller did not take yet is dropped */
	n = MIN(apu->samples_per_frame, SAMPLE_FRAMES_MAX);
	for (int i = 0; i < n; i++) {
		pcm[2 * i] = pcm[2 * i + 1] = apu->buffer[i];
	}
	pcm_frames = n;
	pcm_pos = 0;

	return drawn;
}

size_t nesemu_audio_read(int16_t *out, size_t frames)
{
	size_t n = MIN(frames, pcm_frames - pcm_pos);

	memcpy(out, pcm + pcm_pos * 2, n * 4);
	pcm_pos += n;

	return n;
}

int nesemu_save_flush(void)
{
	nes_t *nes = nes_getptr();
	rom_t *cart = nes->cart;
	struct fs_file_t f;
	size_t len;
	uint32_t crc;
	int ret;

	if (!open_ || save_file[0] == '\0' || cart == NULL || cart->prg_ram == NULL) {
		return 0;
	}
	len = cart->prg_ram_banks * ROM_PRG_BANK_SIZE;
	crc = crc32_ieee(cart->prg_ram, len);
	if (crc == save_crc) {
		return 0;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, save_file, FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC);
	if (ret != 0) {
		return 0;
	}
	ret = fs_write(&f, cart->prg_ram, len);
	fs_close(&f);
	if (ret != (int)len) {
		return 0;
	}
	save_crc = crc;

	return 1;
}

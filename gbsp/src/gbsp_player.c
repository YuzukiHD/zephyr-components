/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * The interface of the gpSP core (core/) in the shape of the mGBA module: open a ROM, run a
 * frame, take the picture, the sound and the save. The core runs 32768 Hz sound; it is resampled
 * to the rate the caller asks for.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

#include "common.h"
#include "gba_cc_lut.h"
#include "gba_memory.h"
#include "input.h"
#include "main.h"
#include "memmap.h"
#include "savestate.h"
#include "sound.h"
#include "video.h"

#include <gbsp_zephyr/gbsp.h>
#include "bios.h"

/* ---- what the core expects its application to define ------------------------------------- */

u32 idle_loop_target_pc = 0xFFFFFFFF;
u32 translation_gate_target_pc[MAX_TRANSLATION_GATES];
u32 translation_gate_targets = 0;
boot_mode selected_boot_mode = boot_game;
u32 skip_next_frame = 0;
int sprite_limit = 1;
#ifdef HAVE_DYNAREC
int dynarec_enable = 1;
#else
int dynarec_enable = 0;
#endif

/* blocks of 1 MiB the ROM buffer may take; the ROM that does not fit is read from the file */
unsigned gbsp_rom_blocks;

void netpacket_poll_receive()
{
}

void netpacket_send(uint16_t client_id, const void *buf, size_t len)
{
}

/* ---- state of the player ----------------------------------------------------------------- */

#define SAVE_BYTES (128 * 1024)
#define OUT_RING   8192

static struct {
	bool open;
	uint16_t *frame;
	int skip, skip_left;
	const char *save_path;
	uint32_t save_crc;
	unsigned int out_rate;
	/* resampler from 32768 Hz */
	uint32_t step;		/* input frames per output frame, 16.16 */
	uint32_t pos;		/* position inside the batch being converted, 16.16 */
	int16_t last[2];
	/* the converted sound */
	int16_t ring[OUT_RING * 2];
	unsigned int rd, wr, count;
} g;

static int read_file(const char *path, void *buf, size_t max, size_t *got)
{
	struct fs_dirent ent;
	struct fs_file_t f;
	ssize_t n;
	int ret;

	/* a file that is not there is normal (the first run has no save): do not make the file
	 * system log an error for it */
	ret = fs_stat(path, &ent);
	if (ret < 0) {
		return ret;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		return ret;
	}
	n = fs_read(&f, buf, max);
	fs_close(&f);
	if (n < 0) {
		return (int)n;
	}
	*got = n;

	return 0;
}

int gbsp_open(const char *rom_path, const char *save_path, unsigned int out_rate)
{
	struct fs_dirent st;
	size_t got;
	int ret;

	if (g.open) {
		return -EALREADY;
	}
	memset(&g, 0, sizeof(g));
	ret = fs_stat(rom_path, &st);
	if (ret < 0) {
		return ret;
	}
	if (st.size == 0 || st.size > CONFIG_GBSP_MAX_ROM_SIZE_MB * 1024UL * 1024UL) {
		return -EFBIG;
	}
	gbsp_rom_blocks = MIN((unsigned)((st.size + 0xFFFFF) >> 20), CONFIG_GBSP_ROM_BUFFER_MB);

	init_gamepak_buffer();
	init_sound();
	if (load_bios("/SD:/gba_bios.bin") != 0) {
		memcpy(bios_rom, open_gba_bios_rom, sizeof(bios_rom));
	}
	memset(gamepak_backup, 0xff, sizeof(gamepak_backup));
	if (load_gamepak(rom_path, FEAT_DISABLE, FEAT_DISABLE, SERIAL_MODE_DISABLED) != 0) {
		return -EINVAL;
	}
	reset_gba();

	g.save_path = save_path;
	if (save_path != NULL && read_file(save_path, gamepak_backup, SAVE_BYTES, &got) == 0) {
		g.save_crc = crc32_ieee(gamepak_backup, SAVE_BYTES);
	} else {
		g.save_crc = crc32_ieee(gamepak_backup, SAVE_BYTES);
	}

	g.out_rate = out_rate;
	g.step = (uint32_t)(((uint64_t)GBA_SOUND_FREQUENCY << 16) / out_rate);
	g.open = true;

	return 0;
}

void gbsp_close(void)
{
	if (!g.open) {
		return;
	}
	gbsp_save_flush();
	memory_term();
	g.open = false;
}

void gbsp_set_frame_buffer(uint16_t *buf)
{
	g.frame = buf;
	gba_screen_pixels = buf;
}

void gbsp_set_frameskip(int skip)
{
	g.skip = skip;
}

/* the bits of GBA_KEY_* are the bits of the key register of the console */
void gbsp_set_keys(uint32_t keys)
{
	set_input_keys(keys & 0x3ff);
}

const uint16_t *gbsp_frame(unsigned int *width, unsigned int *height)
{
	*width = GBSP_WIDTH;
	*height = GBSP_HEIGHT;

	return g.frame;
}

/* ---- sound: linear interpolation from 32768 Hz to the output rate ----------------------- */

static void ring_put(int16_t l, int16_t r)
{
	if (g.count < OUT_RING) {
		g.ring[g.wr * 2] = l;
		g.ring[g.wr * 2 + 1] = r;
		g.wr = (g.wr + 1) % OUT_RING;
		g.count++;
	}
}

static void resample(const int16_t *in, unsigned int n)
{
	/* y[0] is the last sample of the batch before, y[1..n] the new ones */
	while ((g.pos >> 16) < n) {
		unsigned int i = g.pos >> 16;
		int32_t f = (int32_t)(g.pos & 0xFFFF);
		int16_t a[2], b[2];

		if (i == 0) {
			a[0] = g.last[0];
			a[1] = g.last[1];
		} else {
			a[0] = in[(i - 1) * 2];
			a[1] = in[(i - 1) * 2 + 1];
		}
		b[0] = in[i * 2];
		b[1] = in[i * 2 + 1];
		ring_put((int16_t)(a[0] + (((int32_t)(b[0] - a[0]) * f) >> 16)),
			 (int16_t)(a[1] + (((int32_t)(b[1] - a[1]) * f) >> 16)));
		g.pos += g.step;
	}
	g.pos -= n << 16;
	if (n > 0) {
		g.last[0] = in[(n - 1) * 2];
		g.last[1] = in[(n - 1) * 2 + 1];
	}
}

int gbsp_run_frame(void)
{
	static int16_t mix[(GBA_SOUND_FREQUENCY / 60 + 64) * 2];
	bool drawn;
	u32 n;

	if (!g.open) {
		return 0;
	}
	drawn = g.skip_left == 0;
	skip_next_frame = drawn ? 0 : 1;
	if (g.skip_left > 0) {
		g.skip_left--;
	} else {
		g.skip_left = g.skip;
	}

	update_input();
	rumble_frame_reset();
	clear_gamepak_stickybits();
	gba_execute_frame(execute_cycles);

	n = sound_read_samples(mix, GBA_SOUND_FREQUENCY / 60 + 64);
	resample(mix, n);

	return drawn;
}

size_t gbsp_audio_read(int16_t *out, size_t frames)
{
	size_t n = MIN(frames, (size_t)g.count);

	for (size_t i = 0; i < n; i++) {
		out[i * 2] = g.ring[g.rd * 2];
		out[i * 2 + 1] = g.ring[g.rd * 2 + 1];
		g.rd = (g.rd + 1) % OUT_RING;
	}
	g.count -= n;

	return n;
}

size_t gbsp_audio_available(void)
{
	return g.count;
}

int gbsp_save_flush(void)
{
	struct fs_file_t f;
	uint32_t crc;
	size_t len;
	int ret;

	if (!g.open || g.save_path == NULL) {
		return 0;
	}
	crc = crc32_ieee(gamepak_backup, SAVE_BYTES);
	if (crc == g.save_crc) {
		return 0;
	}
	/* the part of the save memory that was written */
	for (len = SAVE_BYTES; len > 512 && gamepak_backup[len - 1] == 0xff; len--) {
	}
	len = (len + 511) & ~511UL;
	fs_file_t_init(&f);
	ret = fs_open(&f, g.save_path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
	if (ret < 0) {
		return 0;
	}
	ret = fs_write(&f, gamepak_backup, len);
	fs_close(&f);
	if (ret < 0) {
		return 0;
	}
	g.save_crc = crc;

	return 1;
}

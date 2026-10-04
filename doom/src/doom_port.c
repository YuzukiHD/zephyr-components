/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* The platform functions doomgeneric asks for, and the entry points of doom.h */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <doom_zephyr/doom.h>

#include "doomgeneric.h"
#include "i_video.h"

#define KEYQUEUE_SIZE 64

extern char doom_base_dir[64];
/* the palette of the engine in the 8 bit mode (i_video.c) */
extern boolean palette_changed;

static uint16_t *out_buf;
static uint16_t palette565[256];
static int drawn;

static uint16_t keyq[KEYQUEUE_SIZE];
static atomic_t key_w, key_r;

static char *argv_[12];
static char iwad_arg[128], config_arg[96];

void DG_Init(void)
{
	palette_changed = true;
}

void DG_DrawFrame(void)
{
	const uint8_t *src = (const uint8_t *)DG_ScreenBuffer;

	if (palette_changed) {
		for (int i = 0; i < 256; i++) {
			palette565[i] = ((colors[i].r & 0xf8) << 8) | ((colors[i].g & 0xfc) << 3) |
					(colors[i].b >> 3);
		}
		palette_changed = false;
	}
	if (out_buf == NULL) {
		return;
	}
	for (int i = 0; i < DOOM_WIDTH * DOOM_HEIGHT; i++) {
		out_buf[i] = palette565[src[i]];
	}
	drawn = 1;
}

void DG_SleepMs(uint32_t ms)
{
	k_msleep(ms);
}

uint32_t DG_GetTicksMs(void)
{
	return k_uptime_get_32();
}

int DG_GetKey(int *pressed, unsigned char *key)
{
	atomic_val_t r = atomic_get(&key_r);
	uint16_t k;

	if (r == atomic_get(&key_w)) {
		return 0;
	}
	k = keyq[r % KEYQUEUE_SIZE];
	atomic_set(&key_r, r + 1);
	*pressed = k >> 8;
	*key = k & 0xff;

	return 1;
}

void DG_SetWindowTitle(const char *title)
{
	printk("doom: %s\n", title);
}

void doom_key(int pressed, unsigned char key)
{
	atomic_val_t w = atomic_get(&key_w);

	if (w - atomic_get(&key_r) >= KEYQUEUE_SIZE) {
		return;
	}
	keyq[w % KEYQUEUE_SIZE] = (pressed ? 0x100 : 0) | key;
	atomic_set(&key_w, w + 1);
}

void doom_set_frame_buffer(uint16_t *buf)
{
	out_buf = buf;
}

int doom_open(const char *iwad_path, const char *base_dir)
{
	int argc = 0;

	if (iwad_path == NULL || strlen(iwad_path) >= sizeof(iwad_arg)) {
		return -EINVAL;
	}
	if (base_dir != NULL) {
		strncpy(doom_base_dir, base_dir, sizeof(doom_base_dir) - 1);
	} else {
		char *slash;

		strncpy(doom_base_dir, iwad_path, sizeof(doom_base_dir) - 1);
		slash = strrchr(doom_base_dir, '/');
		if (slash != NULL && slash > doom_base_dir) {
			*slash = '\0';
		}
	}
	snprintf(iwad_arg, sizeof(iwad_arg), "%s", iwad_path);
	snprintf(config_arg, sizeof(config_arg), "%s/doom.cfg", doom_base_dir);

	argv_[argc++] = "doom";
	argv_[argc++] = "-iwad";
	argv_[argc++] = iwad_arg;
	argv_[argc++] = "-config";
	argv_[argc++] = config_arg;
	argv_[argc++] = "-mb";
	argv_[argc++] = STRINGIFY(CONFIG_DOOM_ZONE_SIZE_MB);
	argv_[argc] = NULL;

	doomgeneric_Create(argc, argv_);

	return 0;
}

int doom_run_frame(void)
{
	drawn = 0;
	doomgeneric_Tick();

	return drawn;
}

#ifndef CONFIG_DOOM_SOUND
void doom_audio_init(unsigned int rate)
{
}

size_t doom_audio_read(int16_t *out, size_t frames)
{
	memset(out, 0, frames * 4);

	return frames;
}
#endif

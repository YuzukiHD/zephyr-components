/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Sound effects for the engine: the lumps (8 bit unsigned mono) are mixed on the
 * fly into 16 bit stereo at the output rate, doom_audio_read() is called from the
 * audio thread of the application. The music is not played.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <doom_zephyr/doom.h>

#include "deh_str.h"
#include "doomtype.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#define NUM_CHANNELS 8

/* config variables the engine binds; there is no resampling library behind them */
int use_libsamplerate;
float libsamplerate_scale = 0.65f;

struct channel {
	const uint8_t *data;
	unsigned int len;	/* samples */
	uint32_t pos;		/* 16.16 */
	uint32_t step;		/* 16.16 source samples per output frame */
	int left, right;	/* 0..255 */
	int lump;
	bool active;
};

static struct channel ch[NUM_CHANNELS];
static K_MUTEX_DEFINE(lock);
static unsigned int out_rate = 48000;
static bool use_prefix;
static bool ready;

void doom_audio_init(unsigned int rate)
{
	out_rate = rate;
}

/* called with the lock held */
static void channel_release(int i)
{
	if (ch[i].data != NULL) {
		ch[i].active = false;
		ch[i].data = NULL;
		W_ReleaseLumpNum(ch[i].lump);
	}
}

size_t doom_audio_read(int16_t *out, size_t frames)
{
	memset(out, 0, frames * 4);
	if (!ready) {
		return frames;
	}
	k_mutex_lock(&lock, K_FOREVER);
	for (int c = 0; c < NUM_CHANNELS; c++) {
		struct channel *s = &ch[c];

		if (!s->active) {
			continue;
		}
		for (size_t i = 0; i < frames; i++) {
			uint32_t idx = s->pos >> 16;
			int a, b, v, frac;

			if (idx + 1 >= s->len) {
				s->active = false;
				break;
			}
			/* linear interpolation between two samples */
			frac = (s->pos >> 8) & 0xff;
			a = (int)s->data[idx] - 128;
			b = (int)s->data[idx + 1] - 128;
			v = (a * (256 - frac) + b * frac);	/* 8.8 */
			for (int side = 0; side < 2; side++) {
				int gain = side == 0 ? s->left : s->right;
				int mixed = out[2 * i + side] + (v * gain) / 255;

				out[2 * i + side] = CLAMP(mixed, INT16_MIN, INT16_MAX);
			}
			s->pos += s->step;
		}
	}
	k_mutex_unlock(&lock);

	return frames;
}

static void sfx_name(sfxinfo_t *sfx, char *buf, size_t len)
{
	if (sfx->link != NULL) {
		sfx = sfx->link;
	}
	if (use_prefix) {
		M_snprintf(buf, len, "ds%s", DEH_String(sfx->name));
	} else {
		M_StringCopy(buf, DEH_String(sfx->name), len);
	}
}

static boolean snd_init(boolean use_sfx_prefix)
{
	use_prefix = use_sfx_prefix;
	ready = true;

	return true;
}

static void snd_shutdown(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	ready = false;
	for (int i = 0; i < NUM_CHANNELS; i++) {
		channel_release(i);
	}
	k_mutex_unlock(&lock);
}

static int snd_lump(sfxinfo_t *sfx)
{
	char name[9];

	sfx_name(sfx, name, sizeof(name));

	return W_GetNumForName(name);
}

static void snd_params(int c, int vol, int sep)
{
	int left = ((254 - sep) * vol) / 127;
	int right = (sep * vol) / 127;

	if (c < 0 || c >= NUM_CHANNELS) {
		return;
	}
	ch[c].left = CLAMP(left, 0, 255);
	ch[c].right = CLAMP(right, 0, 255);
}

static int snd_start(sfxinfo_t *sfx, int c, int vol, int sep)
{
	const uint8_t *lump;
	unsigned int lumplen, length;
	int rate;

	if (!ready || c < 0 || c >= NUM_CHANNELS) {
		return -1;
	}
	lump = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
	lumplen = W_LumpLength(sfx->lumpnum);
	if (lumplen < 8 || lump[0] != 0x03 || lump[1] != 0x00) {
		W_ReleaseLumpNum(sfx->lumpnum);
		return -1;
	}
	rate = (lump[3] << 8) | lump[2];
	length = (lump[7] << 24) | (lump[6] << 16) | (lump[5] << 8) | lump[4];
	if (length > lumplen - 8 || length <= 48 || rate == 0) {
		W_ReleaseLumpNum(sfx->lumpnum);
		return -1;
	}

	k_mutex_lock(&lock, K_FOREVER);
	channel_release(c);
	/* the first and the last 16 samples of a lump are not played */
	ch[c].data = lump + 8 + 16;
	ch[c].len = length - 32;
	ch[c].lump = sfx->lumpnum;
	ch[c].pos = 0;
	ch[c].step = ((uint64_t)rate << 16) / out_rate;
	snd_params(c, vol, sep);
	ch[c].active = true;
	k_mutex_unlock(&lock);

	return c;
}

static void snd_stop(int c)
{
	if (c < 0 || c >= NUM_CHANNELS) {
		return;
	}
	k_mutex_lock(&lock, K_FOREVER);
	channel_release(c);
	k_mutex_unlock(&lock);
}

static boolean snd_playing(int c)
{
	return c >= 0 && c < NUM_CHANNELS && ch[c].active;
}

/* the lumps of finished sounds go back to the cache */
static void snd_update(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < NUM_CHANNELS; i++) {
		if (!ch[i].active) {
			channel_release(i);
		}
	}
	k_mutex_unlock(&lock);
}

static void snd_cache(sfxinfo_t *sounds, int n)
{
}

static snddevice_t sound_devices[] = {SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
				      SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32};

sound_module_t DG_sound_module = {
	sound_devices, ARRAY_SIZE(sound_devices), snd_init, snd_shutdown, snd_lump,
	snd_update, snd_params, snd_start, snd_stop, snd_playing, snd_cache,
};

/* no music: every call does nothing, and no song ever plays */
static boolean mus_init(void) { return true; }
static void mus_nop(void) { }
static void mus_volume(int volume) { }
static void *mus_register(void *data, int len) { return NULL; }
static void mus_unregister(void *handle) { }
static void mus_play(void *handle, boolean looping) { }
static boolean mus_playing(void) { return false; }

static snddevice_t music_devices[] = {SNDDEVICE_SB};

music_module_t DG_music_module = {
	music_devices, ARRAY_SIZE(music_devices), mus_init, mus_nop, mus_volume, mus_nop, mus_nop,
	mus_register, mus_unregister, mus_play, mus_nop, mus_playing, mus_nop,
};

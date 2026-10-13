/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The MP4 player behind the user interface. The H.264 track goes through the video engine onto
 * the video plane, the AAC track is decoded in software and played by the codec; the audio is the
 * master clock, a picture waits until the sound has reached its time stamp and one that is too
 * late is dropped.
 *
 * Playing, pausing and seeking are all the same thing: a request for a new session at a position,
 * playing or not. The video and the audio thread each watch the sequence number of the request
 * and start over when it changes. A paused session shows the picture at its position and holds.
 */

#include <errno.h>
#include <pvmp4audiodecoder_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/i2s_sunxi.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/mp4.h>
#include <zephyr/sys/printk.h>

#include "player/player_priv.h"

struct mp4 pl_mp4;
struct reader pl_vfile, pl_afile;
bool pl_opened;

K_THREAD_STACK_DEFINE(audio_stack, 8192);
K_THREAD_STACK_DEFINE(video_stack, 8192);
struct k_thread pl_audio_thread;
struct k_thread pl_video_thread;

const struct device *const pl_codec_i2s = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
const struct device *const pl_codec_ctl = DEVICE_DT_GET(DT_NODELABEL(codec_analog));
const struct device *const pl_vdec_dev = DEVICE_DT_GET(DT_NODELABEL(ve));
const struct device *const pl_disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

struct k_spinlock pl_req_lock;
struct request pl_req;
volatile uint32_t pl_pos_ms;
/* the session whose first picture is decoded: the sound of a session starts when it is set */
volatile uint32_t pl_video_ready_seq = UINT32_MAX;

volatile bool pl_quit;
volatile enum player_state pl_state = PLAYER_IDLE;
volatile int pl_volume_pct = 70;

/* audio state, written by the audio thread */
volatile bool pl_audio_present;
volatile bool pl_audio_running;
volatile bool pl_audio_eof;
volatile int64_t pl_audio_base_us;	/* position of the first block of the session */
volatile int64_t pl_audio_end_us;	/* where the sound stopped at the end of the file */
volatile int64_t pl_audio_end_ms;	/* uptime at that moment */

/* ---- requests ------------------------------------------------------------------------------ */

struct request pl_req_get(void)
{
	k_spinlock_key_t key = k_spin_lock(&pl_req_lock);
	struct request r = pl_req;

	k_spin_unlock(&pl_req_lock, key);

	return r;
}

void pl_req_post(int64_t pos_us, bool play)
{
	k_spinlock_key_t key = k_spin_lock(&pl_req_lock);

	pl_req.seq++;
	pl_req.pos_us = pos_us;
	pl_req.play = play;
	/* the position shown follows the request at once, a session of the old request can no
	 * longer overwrite it */
	pl_pos_ms = pos_us / 1000;
	k_spin_unlock(&pl_req_lock, key);
}

/* A session reports the picture it put up; ignored when a newer request has come in */
bool pl_pos_publish(uint32_t seq, uint32_t ms)
{
	k_spinlock_key_t key = k_spin_lock(&pl_req_lock);
	bool current = pl_req.seq == seq;

	if (current) {
		pl_pos_ms = ms;
	}
	k_spin_unlock(&pl_req_lock, key);

	return current;
}

bool pl_req_changed(uint32_t seq)
{
	return pl_quit || pl_req_get().seq != seq;
}

/* ---- the file ------------------------------------------------------------------------------ */

static const struct player_growing *pl_grow;
static char pl_path[128];

/* waits until [off, off + len) is in the file; reopens the handle when it is older than the data */
static int pl_grow_wait(struct reader *r, uint64_t off, size_t len)
{
	uint64_t need = off + len, av;
	int st;

	for (;;) {
		av = pl_grow->avail(pl_grow->ctx, &st);
		if (st == 1) {
			av = pl_grow->total;
		}
		if (av >= need) {
			break;
		}
		if (st < 0) {
			printk("player: the download state is %d with %llu of the %llu bytes needed\n", st,
			       (unsigned long long)av, (unsigned long long)need);
			return -EIO;
		}
		k_msleep(20);
	}
	if (need > r->known) {
		int ret;

		fs_close(&r->f);
		fs_file_t_init(&r->f);
		ret = fs_open(&r->f, pl_path, FS_O_READ);
		if (ret != 0) {
			return ret;
		}
		r->known = st == 1 ? UINT64_MAX : av;
	}

	return 0;
}

int pl_file_read(void *ctx, uint64_t off, void *buf, size_t len)
{
	struct reader *r = ctx;
	int ret;
	ssize_t n;

	if (pl_grow != NULL && !(r->win_len > 0 && off >= r->win_off && off + len <= r->win_off + r->win_len)) {
		ret = pl_grow_wait(r, off, len);
		if (ret != 0) {
			return ret;
		}
	}
	if (len > r->win_size) {
		ret = fs_seek(&r->f, (off_t)off, FS_SEEK_SET);
		n = ret == 0 ? fs_read(&r->f, buf, len) : ret;
		return n == (ssize_t)len ? 0 : (n < 0 ? (int)n : -EIO);
	}
	if (off < r->win_off || off + len > r->win_off + r->win_len) {
		ret = fs_seek(&r->f, (off_t)off, FS_SEEK_SET);
		if (ret != 0) {
			return ret;
		}
		n = fs_read(&r->f, r->win, r->win_size);
		if (n < (ssize_t)len) {
			r->win_len = 0;
			return n < 0 ? (int)n : -EIO;
		}
		r->win_off = off;
		r->win_len = n;
	}
	memcpy(buf, r->win + (off - r->win_off), len);

	return 0;
}

void pl_reader_close(struct reader *r)
{
	if (r->win != NULL) {
		fs_close(&r->f);
		free(r->win);
		r->win = NULL;
	}
	r->win_len = 0;
}

int pl_reader_open(struct reader *r, const char *path, size_t win_size)
{
	int ret;

	fs_file_t_init(&r->f);
	r->win_size = win_size;
	r->win_off = 0;
	r->win_len = 0;
	r->known = UINT64_MAX;
	r->win = aligned_alloc(64, win_size);
	if (r->win == NULL) {
		return -ENOMEM;
	}
	ret = fs_open(&r->f, path, FS_O_READ);
	if (ret != 0) {
		free(r->win);
		r->win = NULL;
	}

	return ret;
}

/* ---- volume -------------------------------------------------------------------------------- */

void pl_volume_apply(void)
{
	audio_property_value_t mute = {.mute = pl_volume_pct == 0};
	audio_property_value_t vol = {.vol = pl_volume_pct * VOLUME_MAX_REG / 100};

	audio_codec_set_property(pl_codec_ctl, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL, vol);
	audio_codec_set_property(pl_codec_ctl, AUDIO_PROPERTY_OUTPUT_MUTE, AUDIO_CHANNEL_ALL, mute);
}

void player_set_volume(int percent)
{
	pl_volume_pct = CLAMP(percent, 0, 100);
	pl_volume_apply();
}

int player_volume(void)
{
	return pl_volume_pct;
}


/* ---- control ------------------------------------------------------------------------------- */

static int open_impl(const char *path, const struct player_growing *g, struct player_info *info)
{
	const struct mp4_track *v;
	struct fs_dirent st;
	int ret;

	if (pl_opened) {
		player_close();
	}
	if (!device_is_ready(pl_vdec_dev) || !device_is_ready(pl_codec_i2s) || !device_is_ready(pl_codec_ctl)) {
		return -ENODEV;
	}
	pl_grow = g;
	if (g != NULL) {
		/* the handles are opened by the download before; they know nothing of the data to come */
		st.size = g->total;
		snprintf(pl_path, sizeof(pl_path), "%s", path);
		ret = 0;
	} else {
		ret = fs_stat(path, &st);
	}
	if (ret == 0) {
		ret = pl_reader_open(&pl_vfile, path, 128 * 1024);
	}
	if (ret == 0) {
		ret = pl_reader_open(&pl_afile, path, 32 * 1024);
	}
	if (ret == 0 && g != NULL) {
		pl_vfile.known = pl_afile.known = 0;
	}
	if (ret != 0) {
		printk("player: cannot open the files: %d\n", ret);
		goto fail;
	}
	pl_raw_mode = pl_is_raw_name(path);
	if (pl_raw_mode) {
		struct request none = {0};

		ARG_UNUSED(none);
		pl_raw_size = st.size;
		ret = pl_raw_probe(st.size);
		if (ret != 0) {
			goto fail;
		}
		pl_audio_present = false;
		if (info != NULL) {
			info->duration_ms = pl_raw_duration_us / 1000;
			info->width = pl_raw_w;
			info->height = pl_raw_h;
			info->has_audio = false;
			info->raw = true;
		}
		pl_quit = false;
		pl_audio_eof = true;
		pl_audio_running = false;
		pl_audio_end_us = -1;
		pl_video_ready_seq = UINT32_MAX;
		pl_state = PLAYER_PLAYING;
		pl_req.seq = 0;
		pl_req_post(0, true);
		k_thread_create(&pl_video_thread, video_stack, K_THREAD_STACK_SIZEOF(video_stack),
				pl_raw_main, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
		k_thread_name_set(&pl_video_thread, "pl_video");
		pl_opened = true;
		printk("player: %s, %ux%u, %u us per picture, about %u s, no sound\n", path, pl_raw_w,
		       pl_raw_h, pl_raw_period_us, (unsigned)(pl_raw_duration_us / 1000000));

		return 0;
	}
	memset(&pl_mp4, 0, sizeof(pl_mp4));
	ret = mp4_open(&pl_mp4, pl_file_read, &pl_vfile, st.size);
	if (ret != 0 || pl_mp4.video.codec != MP4_CODEC_H264) {
		printk("player: mp4_open returned %d (video codec %d)\n", ret, (int)pl_mp4.video.codec);
		ret = ret != 0 ? ret : -ENOTSUP;
		goto fail;
	}
	v = &pl_mp4.video;
	ret = pl_avcc_parameter_sets(v->extra, v->extra_len);
	if (ret != 0) {
		goto fail_mp4;
	}

	pl_audio_present = pl_mp4.audio.codec == MP4_CODEC_AAC &&
			(pl_mp4.audio.sample_rate == 48000U || pl_mp4.audio.sample_rate == 44100U);
	if (info != NULL) {
		info->duration_ms = v->duration * 1000ULL / v->timescale;
		info->width = v->width;
		info->height = v->height;
		info->has_audio = pl_audio_present;
	}

	pl_quit = false;
	pl_audio_eof = !pl_audio_present;
	pl_audio_running = false;
	pl_audio_end_us = -1;
	pl_video_ready_seq = UINT32_MAX;
	pl_state = PLAYER_PLAYING;
	pl_req.seq = 0;
	pl_req_post(0, true);
	k_thread_create(&pl_video_thread, video_stack, K_THREAD_STACK_SIZEOF(video_stack), pl_video_main,
			NULL, NULL, NULL, 6, 0, K_NO_WAIT);
	k_thread_name_set(&pl_video_thread, "pl_video");
	if (pl_audio_present) {
		k_thread_create(&pl_audio_thread, audio_stack, K_THREAD_STACK_SIZEOF(audio_stack),
				pl_audio_main, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
		k_thread_name_set(&pl_audio_thread, "pl_audio");
	}
	pl_opened = true;
	printk("player: %s, %ux%u, %u frames, %s\n", path, v->width, v->height, v->sample_count,
	       pl_audio_present ? "with sound" : "no sound");

	return 0;

fail_mp4:
	mp4_close(&pl_mp4);
fail:
	pl_reader_close(&pl_vfile);
	pl_reader_close(&pl_afile);
	pl_grow = NULL;

	return ret;
}

int player_open(const char *path, struct player_info *info)
{
	return open_impl(path, NULL, info);
}

int player_open_growing(const char *path, const struct player_growing *g, struct player_info *info)
{
	return open_impl(path, g, info);
}

void player_close(void)
{
	if (!pl_opened) {
		return;
	}
	printk("player: closing\n");
	pl_quit = true;
	k_thread_join(&pl_video_thread, K_SECONDS(5));
	printk("player: the video thread is gone\n");
	if (pl_audio_present) {
		k_thread_join(&pl_audio_thread, K_SECONDS(5));
		printk("player: the audio thread is gone\n");
	}
	display_sunxi_hide_yuv(pl_disp);
	printk("player: the picture is off the screen\n");
	if (!pl_raw_mode) {
		mp4_close(&pl_mp4);
	}
	pl_reader_close(&pl_vfile);
	pl_reader_close(&pl_afile);
	pl_grow = NULL;
	pl_opened = false;
	pl_state = PLAYER_IDLE;
	printk("player: closed\n");
}

void player_play(void)
{
	if (!pl_opened) {
		return;
	}
	if (pl_state == PLAYER_ENDED) {
		pl_state = PLAYER_PLAYING;
		pl_req_post(0, true);
	} else if (pl_state == PLAYER_PAUSED) {
		pl_state = PLAYER_PLAYING;
		pl_req_post((int64_t)pl_pos_ms * 1000, true);
	}
}

void player_pause(void)
{
	if (pl_opened && pl_state == PLAYER_PLAYING) {
		pl_state = PLAYER_PAUSED;
		pl_req_post((int64_t)pl_pos_ms * 1000, false);
	}
}

void player_seek(uint32_t ms)
{
	if (!pl_opened) {
		return;
	}
	if (pl_state == PLAYER_ENDED) {
		pl_state = PLAYER_PAUSED;
	}
	pl_req_post((int64_t)ms * 1000, pl_state == PLAYER_PLAYING);
}

enum player_state player_state(void)
{
	return pl_state;
}

uint32_t player_position_ms(void)
{
	return pl_pos_ms;
}


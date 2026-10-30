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

#include "features/video/player_priv.h"

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
volatile int64_t pl_audio_queued_us; /* clock cannot pass the samples queued to the codec */

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
	uint64_t movie_size;
	int ret;

	if (pl_opened) {
		player_close();
	}
	if (!device_is_ready(pl_vdec_dev) || !device_is_ready(pl_codec_i2s) || !device_is_ready(pl_codec_ctl)) {
		return -ENODEV;
	}
	ret = fs_stat(path, &st);
	if (ret == 0) {
		ret = pl_reader_open(&pl_vfile, path, 256 * 1024);
	}
	if (ret == 0) {
		ret = pl_reader_open(&pl_afile, path, 64 * 1024);
	}
	if (ret != 0) {
		goto fail;
	}
	pl_raw_mode = pl_is_raw_name(path);
	if (g != NULL) {
		if (pl_raw_mode) {
			ret = -ENOTSUP;
			goto fail;
		}
		pl_reader_growing(&pl_vfile, g);
		pl_reader_growing(&pl_afile, g);
		ret = pl_reader_movie_size(&pl_vfile, &movie_size);
		if (ret != 0) {
			goto fail;
		}
	} else {
		movie_size = st.size;
	}
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
		k_thread_name_set(&pl_video_thread, "nekobox_video");
		pl_opened = true;
		printk("player: %s, %ux%u, %u us per picture, about %u s, no sound\n", path, pl_raw_w,
		       pl_raw_h, pl_raw_period_us, (unsigned)(pl_raw_duration_us / 1000000));

		return 0;
	}
	memset(&pl_mp4, 0, sizeof(pl_mp4));
	ret = mp4_open(&pl_mp4, pl_file_read, &pl_vfile, movie_size);
	if (ret != 0 || pl_mp4.video.codec != MP4_CODEC_H264) {
		ret = ret != 0 ? ret : -ENOTSUP;
		goto fail_mp4;
	}
	v = &pl_mp4.video;
	ret = player_growing_check_resolution(g, v->width, v->height);
	if (ret != 0) {
		goto fail_mp4;
	}
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
		info->raw = false;
	}

	pl_quit = false;
	pl_audio_eof = !pl_audio_present;
	pl_audio_running = false;
	pl_audio_end_us = -1;
	pl_video_ready_seq = UINT32_MAX;
	pl_vfile.opening = pl_afile.opening = false;
	pl_vfile.seq = pl_afile.seq = 1;
	pl_state = g != NULL ? PLAYER_PAUSED : PLAYER_PLAYING;
	pl_req.seq = 0;
	pl_req_post(0, g == NULL);
	k_thread_create(&pl_video_thread, video_stack, K_THREAD_STACK_SIZEOF(video_stack), pl_video_main,
			NULL, NULL, NULL, 6, 0, K_NO_WAIT);
	k_thread_name_set(&pl_video_thread, "nekobox_video");
	if (pl_audio_present) {
		k_thread_create(&pl_audio_thread, audio_stack, K_THREAD_STACK_SIZEOF(audio_stack),
				pl_audio_main, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
		k_thread_name_set(&pl_audio_thread, "nekobox_audio");
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

	return ret;
}

int player_open(const char *path, struct player_info *info)
{
	return open_impl(path, NULL, info);
}

int player_open_growing(const char *path, const struct player_growing *g, struct player_info *info)
{
	if (g == NULL || g->avail == NULL) {
		return -EINVAL;
	}
	return open_impl(path, g, info);
}

void player_close(void)
{
	if (!pl_opened) {
		return;
	}
	pl_quit = true;
	k_thread_join(&pl_video_thread, K_SECONDS(5));
	if (pl_audio_present) {
		k_thread_join(&pl_audio_thread, K_SECONDS(5));
	}
	display_sunxi_hide_yuv(pl_disp);
	if (!pl_raw_mode) {
		mp4_close(&pl_mp4);
	}
	pl_reader_close(&pl_vfile);
	pl_reader_close(&pl_afile);
	pl_opened = false;
	pl_state = PLAYER_IDLE;
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


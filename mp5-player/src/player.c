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

#include "player.h"

#define AAC_FRAMES	1024
#define PCM_BLOCK	(AAC_FRAMES * 2 * sizeof(int16_t))
#define PCM_BLOCKS	12
#define PREFILL		6
/*
 * Pictures later than this behind the sound are not shown. Dropping one does not make the
 * decoder faster; a wider limit (8 frames) was tried and left the picture a quarter of a second
 * behind the sound for the rest of the film, which is worse than a missing picture.
 */
#define DROP_US		(2 * 33367)
/* pictures are put up this much before their time, the display waits for a refresh */
#define LEAD_US		8000
#define VOLUME_MAX_REG	200

/*
 * A file with a read window: the samples of a track are small and spread over the file, a seek in
 * FAT walks the cluster chain, so a window is read at a time and the samples are cut out of it.
 */
struct reader {
	struct fs_file_t f;
	uint8_t *win;
	size_t win_size;
	uint64_t win_off;
	size_t win_len;
};

/* A request for a session: where to start and whether to play */
struct request {
	uint32_t seq;
	int64_t pos_us;
	bool play;
};

static struct mp4 mp4;
static struct reader vfile, afile;
static bool opened;

K_MEM_SLAB_DEFINE_STATIC(pcm_slab, PCM_BLOCK, PCM_BLOCKS, 4);
K_THREAD_STACK_DEFINE(audio_stack, 8192);
K_THREAD_STACK_DEFINE(video_stack, 8192);
static struct k_thread audio_thread, video_thread;

#ifdef CONFIG_SAMPLE_MP5_THREAD_STATS
static uint64_t cpu_ms(struct k_thread *t)
{
	k_thread_runtime_stats_t st;

	if (k_thread_runtime_stats_get(t, &st) != 0) {
		return 0;
	}
	return k_cyc_to_ms_floor64(st.execution_cycles);
}
#else
struct stats_none {
	uint64_t execution_cycles, idle_cycles;
};
#define k_thread_runtime_stats_t struct stats_none

static uint64_t cpu_ms(struct k_thread *t)
{
	ARG_UNUSED(t);
	return 0;
}

static inline void stats_none_all_get(struct stats_none *s)
{
	s->execution_cycles = s->idle_cycles = 0;
}
#define k_thread_runtime_stats_all_get stats_none_all_get
#endif

static const struct device *const codec_i2s = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
static const struct device *const codec_ctl = DEVICE_DT_GET(DT_NODELABEL(codec_analog));
static const struct device *const vdec_dev = DEVICE_DT_GET(DT_NODELABEL(ve));
static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static struct k_spinlock req_lock;
static struct request req;
static volatile uint32_t pos_ms;
/* the session whose first picture is decoded: the sound of a session starts when it is set */
static volatile uint32_t video_ready_seq = UINT32_MAX;

static volatile bool quit;
static volatile enum player_state state = PLAYER_IDLE;
static volatile int volume_pct = 70;

/* audio state, written by the audio thread */
static volatile bool audio_present;
static volatile bool audio_running;
static volatile bool audio_eof;
static volatile int64_t audio_base_us;	/* position of the first block of the session */
static volatile int64_t audio_end_us;	/* where the sound stopped at the end of the file */
static volatile int64_t audio_end_ms;	/* uptime at that moment */

/* ---- requests ------------------------------------------------------------------------------ */

static struct request req_get(void)
{
	k_spinlock_key_t key = k_spin_lock(&req_lock);
	struct request r = req;

	k_spin_unlock(&req_lock, key);

	return r;
}

static void req_post(int64_t pos_us, bool play)
{
	k_spinlock_key_t key = k_spin_lock(&req_lock);

	req.seq++;
	req.pos_us = pos_us;
	req.play = play;
	/* the position shown follows the request at once, a session of the old request can no
	 * longer overwrite it */
	pos_ms = pos_us / 1000;
	k_spin_unlock(&req_lock, key);
}

/* A session reports the picture it put up; ignored when a newer request has come in */
static bool pos_publish(uint32_t seq, uint32_t ms)
{
	k_spinlock_key_t key = k_spin_lock(&req_lock);
	bool current = req.seq == seq;

	if (current) {
		pos_ms = ms;
	}
	k_spin_unlock(&req_lock, key);

	return current;
}

static bool req_changed(uint32_t seq)
{
	return quit || req_get().seq != seq;
}

/* ---- the file ------------------------------------------------------------------------------ */

static int file_read(void *ctx, uint64_t off, void *buf, size_t len)
{
	struct reader *r = ctx;
	int ret;
	ssize_t n;

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

static void reader_close(struct reader *r)
{
	if (r->win != NULL) {
		fs_close(&r->f);
		free(r->win);
		r->win = NULL;
	}
	r->win_len = 0;
}

static int reader_open(struct reader *r, const char *path, size_t win_size)
{
	int ret;

	fs_file_t_init(&r->f);
	r->win_size = win_size;
	r->win_len = 0;
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

static void volume_apply(void)
{
	audio_property_value_t mute = {.mute = volume_pct == 0};
	audio_property_value_t vol = {.vol = volume_pct * VOLUME_MAX_REG / 100};

	audio_codec_set_property(codec_ctl, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL, vol);
	audio_codec_set_property(codec_ctl, AUDIO_PROPERTY_OUTPUT_MUTE, AUDIO_CHANNEL_ALL, mute);
}

void player_set_volume(int percent)
{
	volume_pct = CLAMP(percent, 0, 100);
	volume_apply();
}

int player_volume(void)
{
	return volume_pct;
}

/* ---- audio --------------------------------------------------------------------------------- */

/* Position of the sound in microseconds, -1 while it is not running */
static int64_t audio_clock_us(void)
{
	uint32_t blocks, cycle;

	if (!audio_running) {
		return -1;
	}
	i2s_sunxi_codec_tx_position(codec_i2s, &blocks, &cycle);

	return audio_base_us + (int64_t)blocks * AAC_FRAMES * 1000000 / mp4.audio.sample_rate +
	       (int64_t)k_cyc_to_us_floor64(k_cycle_get_32() - cycle);
}

/* Decodes and plays the session that starts at r.pos_us until the request changes or the file ends */
static void audio_session(const struct request *r, tPVMP4AudioDecoderExternal *ext, void *dec,
			  int16_t *pcm, uint8_t *buf)
{
	const struct mp4_track *t = &mp4.audio;
	struct i2s_config cfg = {
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = t->sample_rate,
		.mem_slab = &pcm_slab,
		.block_size = PCM_BLOCK,
		.timeout = 2000,
	};
	struct mp4_iter it;
	struct mp4_sample s;
	bool priming = true;
	int queued = 0, ret;

	if (i2s_configure(codec_i2s, I2S_DIR_TX, &cfg) != 0) {
		printk("audio: i2s_configure failed\n");
		return;
	}
	if (mp4_seek_time(t, &it, r->pos_us * (int64_t)t->timescale / 1000000) < 0) {
		/* past the end */
		audio_eof = true;
		return;
	}
	/* the decoder needs the sample before as well, its output is thrown away */
	if (it.next > 0U) {
		mp4_seek_sync(t, &it, it.next - 1U);
	}
	audio_running = false;

	while (!req_changed(r->seq) && mp4_next(t, &it, &s) == 0) {
		void *blk;

		if (file_read(&afile, s.offset, buf, s.size) != 0) {
			printk("audio: read error at sample %u\n", s.index);
			break;
		}
		ext->pInputBuffer = buf;
		ext->inputBufferCurrentLength = s.size;
		ext->inputBufferUsedLength = 0;
		ext->remainderBits = 0;
		ret = PVMP4AudioDecodeFrame(ext, dec);
		if (ret != MP4AUDEC_SUCCESS) {
			continue;
		}
		if (priming) {
			priming = false;
			audio_base_us = (s.pts - t->media_start) * 1000000 / t->timescale +
					(int64_t)AAC_FRAMES * 1000000 / t->sample_rate;
			continue;
		}
		if (k_mem_slab_alloc(&pcm_slab, &blk, K_SECONDS(2)) != 0) {
			break;
		}
		memcpy(blk, pcm, PCM_BLOCK);
		ret = i2s_write(codec_i2s, blk, PCM_BLOCK);
		if (ret == -EIO) {
			/* the stream ran dry: start it again where it was */
			int64_t at = audio_clock_us();

			if (at >= 0) {
				audio_base_us = at;
			}
			audio_running = false;
			k_mem_slab_free(&pcm_slab, blk);
			i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			queued = 0;
			continue;
		} else if (ret != 0) {
			k_mem_slab_free(&pcm_slab, blk);
			break;
		}
		if (!audio_running && ++queued >= PREFILL) {
			/* the video has to get from its key frame to the position first, the sound
			 * must not run away from it */
			while (!req_changed(r->seq) && video_ready_seq != r->seq) {
				k_msleep(1);
			}
			if (req_changed(r->seq)) {
				break;
			}
			if (i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_START) == 0) {
				audio_running = true;
			}
		}
	}

	if (req_changed(r->seq)) {
		audio_running = false;
		i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
		return;
	}
	/* the end of the file: let what is queued play out */
	audio_end_us = audio_clock_us();
	audio_end_ms = k_uptime_get();
	i2s_trigger(codec_i2s, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
	k_sleep(K_MSEC(300));
	audio_running = false;
	audio_eof = true;
}

static void audio_main(void *a, void *b, void *c)
{
	const struct mp4_track *t = &mp4.audio;
	tPVMP4AudioDecoderExternal ext = {0};
	void *dec = malloc(PVMP4AudioDecoderGetMemRequirements());
	int16_t *pcm = malloc(sizeof(int16_t) * AAC_FRAMES * 2 * 2);
	uint8_t *buf = malloc(t->max_sample);
	uint32_t done = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	ext.desiredChannels = 2;
	ext.outputFormat = OUTPUTFORMAT_16PCM_INTERLEAVED;
	ext.aacPlusEnabled = false;
	ext.pOutputBuffer = pcm;
	ext.pOutputBuffer_plus = pcm + AAC_FRAMES * 2;
	if (dec == NULL || pcm == NULL || buf == NULL ||
	    PVMP4AudioDecoderInitLibrary(&ext, dec) != 0) {
		printk("audio: cannot start the AAC decoder\n");
		audio_eof = true;
		goto out;
	}
	/* the AudioSpecificConfig of the track tells the decoder the stream layout */
	ext.pInputBuffer = t->extra;
	ext.inputBufferCurrentLength = t->extra_len;
	ext.inputBufferUsedLength = 0;
	ext.remainderBits = 0;
	if (PVMP4AudioDecoderConfig(&ext, dec) != MP4AUDEC_SUCCESS) {
		printk("audio: bad AudioSpecificConfig\n");
		audio_eof = true;
		goto out;
	}
	audio_codec_start_output(codec_ctl);
	volume_apply();

	while (!quit) {
		struct request r = req_get();

		if (r.seq == done) {
			k_msleep(5);
			continue;
		}
		done = r.seq;
		audio_eof = false;
		audio_running = false;
		if (r.play) {
			audio_session(&r, &ext, dec, pcm, buf);
		}
	}
	audio_codec_stop_output(codec_ctl);
out:
	free(dec);
	free(pcm);
	free(buf);
}

/* ---- video --------------------------------------------------------------------------------- */

static uint8_t annexb_head[512];
static size_t annexb_head_len;
static int nal_len_size;

/* SPS and PPS of the avcC record as Annex B */
static int avcc_parameter_sets(const uint8_t *e, uint32_t len)
{
	size_t o = 5, w = 0;
	int n;

	if (len < 7U || e[0] != 1) {
		return -EINVAL;
	}
	nal_len_size = (e[4] & 3) + 1;
	for (int pass = 0; pass < 2; pass++) {
		n = pass == 0 ? (e[o] & 0x1f) : e[o];
		o++;
		for (int i = 0; i < n; i++) {
			size_t l;

			if (o + 2 > len) {
				return -EINVAL;
			}
			l = (e[o] << 8) | e[o + 1];
			o += 2;
			if (o + l > len || w + 4 + l > sizeof(annexb_head)) {
				return -EINVAL;
			}
			annexb_head[w++] = 0;
			annexb_head[w++] = 0;
			annexb_head[w++] = 0;
			annexb_head[w++] = 1;
			memcpy(annexb_head + w, e + o, l);
			w += l;
			o += l;
		}
	}
	annexb_head_len = w;

	return 0;
}

/* Length prefixed NAL units to start code prefixed ones; returns the new size */
static size_t avcc_to_annexb(const uint8_t *in, size_t len, uint8_t *out, bool with_head)
{
	size_t w = 0;

	if (with_head) {
		memcpy(out, annexb_head, annexb_head_len);
		w = annexb_head_len;
	}
	while (len > (size_t)nal_len_size) {
		size_t l = 0;

		for (int i = 0; i < nal_len_size; i++) {
			l = (l << 8) | *in++;
		}
		len -= nal_len_size;
		if (l > len) {
			break;
		}
		out[w++] = 0;
		out[w++] = 0;
		out[w++] = 0;
		out[w++] = 1;
		memcpy(out + w, in, l);
		w += l;
		in += l;
		len -= l;
	}

	return w;
}

/* The first NAL unit types of an Annex B unit, for the log: 5 is an IDR slice, 1 a plain one */
static void nal_types(const uint8_t *p, size_t len, int *idr, int *other)
{
	*idr = *other = 0;
	for (size_t i = 0; i + 4 < len; i++) {
		if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 && p[i + 3] == 1) {
			int t = p[i + 4] & 0x1f;

			if (t == 5) {
				(*idr)++;
			} else if (t >= 1 && t <= 4) {
				(*other)++;
			}
		}
	}
}

static void show(const struct vdec_frame *f)
{
	struct display_sunxi_yuv yuv = {
		.y = f->plane[0],
		.uv = f->plane[1],
		.width = f->width,
		.height = f->height,
		.stride_y = f->stride[0],
		.stride_uv = f->stride[1],
		.bt709 = true,
		/* the display thread waits for the flip, nothing else does */
		.nonblock = false,
	};

	display_sunxi_show_yuv(disp, &yuv);
}

/*
 * Pictures go to the display through a thread: committing one waits for the flip of the previous
 * one (up to two refreshes), which would stall the decoder. The thread hands a picture back once
 * the one after it is up, and the video thread releases it, so the decoder state stays single
 * threaded. A picture without a buffer ends a session: the thread returns what it still holds.
 */
K_MSGQ_DEFINE(disp_q, sizeof(struct vdec_frame), 1, 4);
K_MSGQ_DEFINE(done_q, sizeof(struct vdec_frame), 4, 4);
K_SEM_DEFINE(disp_flushed, 0, 1);

static void disp_main(void *a, void *b, void *c)
{
	struct vdec_frame f, shown = {0};

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		k_msgq_get(&disp_q, &f, K_FOREVER);
		if (f.priv == NULL) {
			/* the plane scans the shown picture out: take it off the screen before its
			 * memory goes back to the pool, or the next session decodes into a picture
			 * that is being displayed
			 */
			if (shown.priv != NULL) {
				display_sunxi_hide_yuv(disp);
				k_msgq_put(&done_q, &shown, K_FOREVER);
			}
			shown = (struct vdec_frame){0};
			k_sem_give(&disp_flushed);
			continue;
		}
		/* the commit returns when the new picture is up: the one before is off the screen */
		show(&f);
		if (shown.priv != NULL) {
			k_msgq_put(&done_q, &shown, K_FOREVER);
		}
		shown = f;
	}
}

K_THREAD_DEFINE(disp_tid, 16384, disp_main, NULL, NULL, NULL, 5, 0, 0);

static void release_done(void)
{
	struct vdec_frame f;

	while (k_msgq_get(&done_q, &f, K_NO_WAIT) == 0) {
		vdec_frame_release(vdec_dev, &f);
	}
}

enum wait_result {
	WAIT_ON_TIME,
	WAIT_LATE,
	WAIT_ABORT,
};

/*
 * Waits for the time of a picture. Without the sound running the clock is the wall clock from
 * the start of the session; after the end of the sound it goes on from where the sound stopped.
 */
static enum wait_result wait_for(uint32_t seq, int64_t pts_us, int64_t start_us, int64_t start_ms)
{
	for (;;) {
		int64_t now = audio_clock_us(), diff;

		if (req_changed(seq)) {
			return WAIT_ABORT;
		}
		if (now < 0) {
			if (audio_eof && audio_end_us >= 0) {
				now = audio_end_us + (k_uptime_get() - audio_end_ms) * 1000;
			} else if (!audio_present || audio_eof) {
				now = start_us + (k_uptime_get() - start_ms) * 1000;
			} else {
				k_msleep(2);
				continue;
			}
		}
		diff = pts_us - now;
		if (diff < -DROP_US) {
			return WAIT_LATE;
		}
		if (diff <= LEAD_US) {
			return WAIT_ON_TIME;
		}
		k_msleep(CLAMP((int)((diff - LEAD_US) / 1000), 1, 20));
	}
}

/* Runs one session of the video until the request changes, or forever once the end is reached */
static void video_session(const struct request *r, uint8_t *raw, uint8_t *ab)
{
	const struct mp4_track *v = &mp4.video;
	const struct vdec_stream_config scfg = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
		.buffer_size = 1024 * 1024,
		/* only the display engine reads the pictures: no cache maintenance for each one */
		.no_cache_ops = true,
	};
	struct vdec_frame frame;
	struct vdec_stream *stream;
	struct mp4_iter it;
	struct mp4_sample s;
	uint32_t it_first;
	size_t ab_len = 0, ab_used = 0;
	int64_t pending_pts = -1, start_ms = k_uptime_get(), key_pts_us = 0;
	bool eof = false, flushed = false, first = true, held = false, shown_any = false;
	bool any_shown = false;
	int ret;

	printk("player: session %u at %d ms, %s\n", r->seq, (int)(r->pos_us / 1000),
	       r->play ? "playing" : "paused");
	if (vdec_stream_open(vdec_dev, &scfg, &stream) != 0) {
		printk("video: cannot open the decoder\n");
		state = PLAYER_ERROR;
		while (!req_changed(r->seq)) {
			k_msleep(20);
		}
		return;
	}
	ret = mp4_seek_time(v, &it, r->pos_us * (int64_t)v->timescale / 1000000);
	if (ret < 0 && ret != -ERANGE) {
		vdec_stream_close(vdec_dev, stream);
		return;
	}

	it_first = it.next;
	int64_t stat_ms = k_uptime_get(), wait_ms = 0, feed_ms = 0;
	uint32_t st_shown = 0, st_late = 0;
	uint64_t st_audio = cpu_ms(&audio_thread), st_video = cpu_ms(&video_thread);
	k_thread_runtime_stats_t st_all0;

	k_thread_runtime_stats_all_get(&st_all0);
	int64_t st_worst_us = 0;
	uint32_t get_us = 0, busy_n = 0, idle_ms = 0;
	while (!req_changed(r->seq)) {
		uint32_t c0;

		release_done();
		c0 = k_cycle_get_32();
		ret = vdec_stream_get_frame(vdec_dev, stream, &frame);
		get_us += k_cyc_to_us_floor32(k_cycle_get_32() - c0);
		if (ret == 0) {
			int64_t pts_us = frame.pts >= 0 ? frame.pts : 0;
			enum wait_result w;

			/* the pictures from the key frame up to the position are not shown, nor the
			 * ones that come before the key frame in display order: they refer to
			 * pictures the decoder never saw
			 */
			if (pts_us + 1000 < r->pos_us || pts_us + 1000 < key_pts_us) {
				vdec_frame_release(vdec_dev, &frame);
				continue;
			}
			if (r->play) {
				video_ready_seq = r->seq;
			}
			int64_t t0 = k_uptime_get();

			w = r->play ? wait_for(r->seq, pts_us, r->pos_us, start_ms) : WAIT_ON_TIME;
			wait_ms += k_uptime_get() - t0;
			if (w == WAIT_ABORT) {
				vdec_frame_release(vdec_dev, &frame);
				break;
			}
			if (w == WAIT_ON_TIME) {
				if (!shown_any) {
					shown_any = true;
					printk("player: session %u first picture at %d ms, %d ms after the start\n",
					       r->seq, (int)(pts_us / 1000), (int)(k_uptime_get() - start_ms));
				}
				k_msgq_put(&disp_q, &frame, K_FOREVER);
				any_shown = true;
				st_shown++;
				pos_publish(r->seq, pts_us / 1000);
				if (!r->play) {
					/* a paused session shows the picture at its position and holds */
					held = true;
					break;
				}
			} else {
				st_late++;
				vdec_frame_release(vdec_dev, &frame);
			}
			if (r->play && k_uptime_get() - stat_ms >= 5000) {
				k_thread_runtime_stats_t all;
				uint64_t a = cpu_ms(&audio_thread), vd = cpu_ms(&video_thread);

				k_thread_runtime_stats_all_get(&all);
				printk("player: per %d ms: %u shown, %u late, wait %d ms, feed %d ms, a/v offset %d ms, get %d ms, %u busy polls, cpu: audio %d video %d busy %d ms\n",
				       (int)(k_uptime_get() - stat_ms), st_shown, st_late, (int)wait_ms,
				       (int)feed_ms, (int)((pts_us - audio_clock_us()) / 1000),
				       get_us / 1000, busy_n, (int)(a - st_audio), (int)(vd - st_video),
				       (int)k_cyc_to_ms_floor64(all.execution_cycles - all.idle_cycles -
								st_all0.execution_cycles + st_all0.idle_cycles));
				get_us = 0;
				busy_n = 0;
				st_audio = a;
				st_video = vd;
				st_all0 = all;
				stat_ms = k_uptime_get();
				wait_ms = feed_ms = 0;
				st_shown = st_late = 0;
			}
			continue;
		}
		if (ret == -ENODATA) {
			/* the end of the file: wait for the sound, then report it */
			video_ready_seq = r->seq;
			while (!req_changed(r->seq) && audio_present && !audio_eof) {
				k_msleep(20);
			}
			if (pos_publish(r->seq, mp4.video.duration * 1000ULL / mp4.video.timescale) &&
			    !req_changed(r->seq)) {
				state = PLAYER_ENDED;
			}
			held = true;
			break;
		}
		if (ret == -EBUSY) {
			busy_n++;
			k_msleep(2);
			continue;
		}
		if (ret != -EAGAIN) {
			printk("video: decode error %d\n", ret);
			state = PLAYER_ERROR;
			held = true;
			break;
		}

		/* the decoder wants data */
		if (ab_used >= ab_len) {
			if (mp4_next(v, &it, &s) != 0) {
				eof = true;
			} else {
				if (file_read(&vfile, s.offset, raw, s.size) != 0) {
					printk("video: read error at sample %u\n", s.index);
					state = PLAYER_ERROR;
					held = true;
					break;
				}
				ab_len = avcc_to_annexb(raw, s.size, ab, first || s.sync);
				ab_used = 0;
				first = false;
				pending_pts = (s.pts - v->media_start) * 1000000 / v->timescale;
				if (pending_pts < 0) {
					pending_pts = 0;
				}
				if (ab_len > 0 && s.index == it_first) {
					int idr, other;

					key_pts_us = pending_pts;
					nal_types(ab, ab_len, &idr, &other);
					printk("player: session %u starts at sample %u (sync %d, pts %d ms, %d IDR, %d other slices)\n",
					       r->seq, s.index, s.sync, (int)(pending_pts / 1000), idr, other);
				}
			}
		}
		if (ab_used < ab_len) {
			size_t used = 0;

			int64_t f0 = k_uptime_get();

			ret = vdec_stream_feed(vdec_dev, stream, ab + ab_used, ab_len - ab_used,
					       pending_pts, &used);
			feed_ms += k_uptime_get() - f0;
			if (ret != 0 && ret != -EAGAIN) {
				ab_used = ab_len;
			} else {
				ab_used += used;
			}
		} else if (eof && !flushed) {
			vdec_stream_flush(vdec_dev, stream);
			flushed = true;
		}
	}

	/* a held session keeps the decoder and its pictures until the next request */
	while (held && !req_changed(r->seq)) {
		k_msleep(20);
	}
	if (any_shown) {
		struct vdec_frame end = {0};

		k_msgq_put(&disp_q, &end, K_FOREVER);
		k_sem_take(&disp_flushed, K_FOREVER);
	}
	release_done();
	vdec_stream_close(vdec_dev, stream);
}

static void video_main(void *a, void *b, void *c)
{
	const struct mp4_track *v = &mp4.video;
	uint8_t *raw = malloc(v->max_sample);
	uint8_t *ab = malloc(v->max_sample + 1024);
	uint32_t done = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	if (raw == NULL || ab == NULL) {
		state = PLAYER_ERROR;
		goto out;
	}
	while (!quit) {
		struct request r = req_get();

		if (r.seq == done) {
			k_msleep(5);
			continue;
		}
		done = r.seq;
		video_session(&r, raw, ab);
	}
out:
	free(raw);
	free(ab);
}

/* ---- control ------------------------------------------------------------------------------- */

int player_open(const char *path, struct player_info *info)
{
	const struct mp4_track *v;
	struct fs_dirent st;
	int ret;

	if (opened) {
		player_close();
	}
	if (!device_is_ready(vdec_dev) || !device_is_ready(codec_i2s) || !device_is_ready(codec_ctl)) {
		return -ENODEV;
	}
	ret = fs_stat(path, &st);
	if (ret == 0) {
		ret = reader_open(&vfile, path, 256 * 1024);
	}
	if (ret == 0) {
		ret = reader_open(&afile, path, 64 * 1024);
	}
	if (ret != 0) {
		goto fail;
	}
	memset(&mp4, 0, sizeof(mp4));
	ret = mp4_open(&mp4, file_read, &vfile, st.size);
	if (ret != 0 || mp4.video.codec != MP4_CODEC_H264) {
		ret = ret != 0 ? ret : -ENOTSUP;
		goto fail;
	}
	v = &mp4.video;
	ret = avcc_parameter_sets(v->extra, v->extra_len);
	if (ret != 0) {
		goto fail_mp4;
	}

	audio_present = mp4.audio.codec == MP4_CODEC_AAC &&
			(mp4.audio.sample_rate == 48000U || mp4.audio.sample_rate == 44100U);
	if (info != NULL) {
		info->duration_ms = v->duration * 1000ULL / v->timescale;
		info->width = v->width;
		info->height = v->height;
		info->has_audio = audio_present;
	}

	quit = false;
	audio_eof = !audio_present;
	audio_running = false;
	audio_end_us = -1;
	video_ready_seq = UINT32_MAX;
	state = PLAYER_PLAYING;
	req.seq = 0;
	req_post(0, true);
	k_thread_create(&video_thread, video_stack, K_THREAD_STACK_SIZEOF(video_stack), video_main,
			NULL, NULL, NULL, 6, 0, K_NO_WAIT);
	k_thread_name_set(&video_thread, "mp5_video");
	if (audio_present) {
		k_thread_create(&audio_thread, audio_stack, K_THREAD_STACK_SIZEOF(audio_stack),
				audio_main, NULL, NULL, NULL, 3, 0, K_NO_WAIT);
		k_thread_name_set(&audio_thread, "mp5_audio");
	}
	opened = true;
	printk("player: %s, %ux%u, %u frames, %s\n", path, v->width, v->height, v->sample_count,
	       audio_present ? "with sound" : "no sound");

	return 0;

fail_mp4:
	mp4_close(&mp4);
fail:
	reader_close(&vfile);
	reader_close(&afile);

	return ret;
}

void player_close(void)
{
	if (!opened) {
		return;
	}
	quit = true;
	k_thread_join(&video_thread, K_SECONDS(5));
	if (audio_present) {
		k_thread_join(&audio_thread, K_SECONDS(5));
	}
	display_sunxi_hide_yuv(disp);
	mp4_close(&mp4);
	reader_close(&vfile);
	reader_close(&afile);
	opened = false;
	state = PLAYER_IDLE;
}

void player_play(void)
{
	if (!opened) {
		return;
	}
	if (state == PLAYER_ENDED) {
		state = PLAYER_PLAYING;
		req_post(0, true);
	} else if (state == PLAYER_PAUSED) {
		state = PLAYER_PLAYING;
		req_post((int64_t)pos_ms * 1000, true);
	}
}

void player_pause(void)
{
	if (opened && state == PLAYER_PLAYING) {
		state = PLAYER_PAUSED;
		req_post((int64_t)pos_ms * 1000, false);
	}
}

void player_seek(uint32_t ms)
{
	if (!opened) {
		return;
	}
	if (state == PLAYER_ENDED) {
		state = PLAYER_PAUSED;
	}
	req_post((int64_t)ms * 1000, state == PLAYER_PLAYING);
}

enum player_state player_state(void)
{
	return state;
}

uint32_t player_position_ms(void)
{
	return pos_ms;
}

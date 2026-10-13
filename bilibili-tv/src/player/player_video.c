/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The picture of an MP4: the H.264 track through the video engine onto the video plane */

#include "debug/crumbs.h"
#include "player/player_priv.h"

#ifdef CONFIG_NEKOBOX_THREAD_STATS
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

static uint8_t annexb_head[512];
static size_t annexb_head_len;
static int nal_len_size;

/* SPS and PPS of the avcC record as Annex B */
int pl_avcc_parameter_sets(const uint8_t *e, uint32_t len)
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

	bili_crumb("v-show");
	display_sunxi_show_yuv(pl_disp, &yuv);
}

/*
 * Pictures go to the display through a thread: committing one waits for the flip of the previous
 * one (up to two refreshes), which would stall the decoder. The thread hands a picture back once
 * the one after it is up, and the video thread releases it, so the decoder pl_state stays single
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
				display_sunxi_hide_yuv(pl_disp);
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

void pl_release_done(void)
{
	struct vdec_frame f;

	while (k_msgq_get(&done_q, &f, K_NO_WAIT) == 0) {
		vdec_frame_release(pl_vdec_dev, &f);
	}
}


/*
 * Waits for the time of a picture. Without the sound running the clock is the wall clock from
 * the start of the session; after the end of the sound it goes on from where the sound stopped.
 */
enum wait_result pl_wait_for(uint32_t seq, int64_t pts_us, int64_t start_us, int64_t start_ms)
{
	for (;;) {
		int64_t now = pl_audio_clock_us(), diff;

		if (pl_req_changed(seq)) {
			return WAIT_ABORT;
		}
		if (now < 0) {
			if (pl_audio_eof && pl_audio_end_us >= 0) {
				now = pl_audio_end_us + (k_uptime_get() - pl_audio_end_ms) * 1000;
			} else if (!pl_audio_present || pl_audio_eof) {
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
static void pl_video_session(const struct request *r, uint8_t *raw, uint8_t *ab)
{
	const struct mp4_track *v = &pl_mp4.video;
	const struct vdec_stream_config scfg = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
		.buffer_size = CONFIG_NEKOBOX_VIDEO_STREAM_KB * 1024,
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
	if (vdec_stream_open(pl_vdec_dev, &scfg, &stream) != 0) {
		printk("video: cannot open the decoder\n");
		pl_state = PLAYER_ERROR;
		while (!pl_req_changed(r->seq)) {
			k_msleep(20);
		}
		return;
	}
	ret = mp4_seek_time(v, &it, r->pos_us * (int64_t)v->timescale / 1000000);
	if (ret < 0 && ret != -ERANGE) {
		vdec_stream_close(pl_vdec_dev, stream);
		return;
	}

	it_first = it.next;
	int64_t stat_ms = k_uptime_get(), wait_ms = 0, feed_ms = 0;
	uint32_t st_shown = 0, st_late = 0;
	uint64_t st_audio = cpu_ms(&pl_audio_thread), st_video = cpu_ms(&pl_video_thread);
	k_thread_runtime_stats_t st_all0;

	k_thread_runtime_stats_all_get(&st_all0);
	uint32_t get_us = 0, busy_n = 0;
	while (!pl_req_changed(r->seq)) {
		uint32_t c0;

		pl_release_done();
		c0 = k_cycle_get_32();
		bili_crumb("v-get");
		ret = vdec_stream_get_frame(pl_vdec_dev, stream, &frame);
		get_us += k_cyc_to_us_floor32(k_cycle_get_32() - c0);
		if (ret == 0) {
			int64_t pts_us = frame.pts >= 0 ? frame.pts : 0;
			enum wait_result w;

			/* the pictures from the key frame up to the position are not shown, nor the
			 * ones that come before the key frame in display order: they refer to
			 * pictures the decoder never saw
			 */
			if (pts_us + 1000 < r->pos_us || pts_us + 1000 < key_pts_us) {
				vdec_frame_release(pl_vdec_dev, &frame);
				continue;
			}
			if (r->play) {
				pl_video_ready_seq = r->seq;
			}
			int64_t t0 = k_uptime_get();

			w = r->play ? pl_wait_for(r->seq, pts_us, r->pos_us, start_ms) : WAIT_ON_TIME;
			wait_ms += k_uptime_get() - t0;
			if (w == WAIT_ABORT) {
				vdec_frame_release(pl_vdec_dev, &frame);
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
				pl_pos_publish(r->seq, pts_us / 1000);
				if (!r->play) {
					/* a paused session shows the picture at its position and holds */
					held = true;
					break;
				}
			} else {
				st_late++;
				vdec_frame_release(pl_vdec_dev, &frame);
			}
			if (r->play && k_uptime_get() - stat_ms >= 5000) {
				k_thread_runtime_stats_t all;
				uint64_t a = cpu_ms(&pl_audio_thread), vd = cpu_ms(&pl_video_thread);

				k_thread_runtime_stats_all_get(&all);
				printk("player: per %d ms: %u shown, %u late, wait %d ms, feed %d ms, a/v offset %d ms, get %d ms, %u busy polls, cpu: audio %d video %d busy %d ms\n",
				       (int)(k_uptime_get() - stat_ms), st_shown, st_late, (int)wait_ms,
				       (int)feed_ms, (int)((pts_us - pl_audio_clock_us()) / 1000),
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
			pl_video_ready_seq = r->seq;
			while (!pl_req_changed(r->seq) && pl_audio_present && !pl_audio_eof) {
				k_msleep(20);
			}
			if (pl_pos_publish(r->seq, pl_mp4.video.duration * 1000ULL / pl_mp4.video.timescale) &&
			    !pl_req_changed(r->seq)) {
				pl_state = PLAYER_ENDED;
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
			pl_state = PLAYER_ERROR;
			held = true;
			break;
		}

		/* the decoder wants data */
		if (ab_used >= ab_len) {
			bili_crumb("v-mp4");
			if (mp4_next(v, &it, &s) != 0) {
				eof = true;
			} else {
				if (pl_file_read(&pl_vfile, s.offset, raw, s.size) != 0) {
					printk("video: read error at sample %u\n", s.index);
					pl_state = PLAYER_ERROR;
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

			bili_crumb("v-feed");
			ret = vdec_stream_feed(pl_vdec_dev, stream, ab + ab_used, ab_len - ab_used,
					       pending_pts, &used);
			feed_ms += k_uptime_get() - f0;
			if (ret != 0 && ret != -EAGAIN) {
				ab_used = ab_len;
			} else {
				ab_used += used;
			}
		} else if (eof && !flushed) {
			vdec_stream_flush(pl_vdec_dev, stream);
			flushed = true;
		}
	}

	/* a held session keeps the decoder and its pictures until the next request */
	while (held && !pl_req_changed(r->seq)) {
		k_msleep(20);
	}
	if (any_shown) {
		struct vdec_frame end = {0};

		k_msgq_put(&disp_q, &end, K_FOREVER);
		k_sem_take(&disp_flushed, K_FOREVER);
	}
	pl_release_done();
	vdec_stream_close(pl_vdec_dev, stream);
}


void pl_video_main(void *a, void *b, void *c)
{
	const struct mp4_track *v = &pl_mp4.video;
	uint8_t *raw = malloc(v->max_sample);
	uint8_t *ab = malloc(v->max_sample + 1024);
	uint32_t done = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	if (raw == NULL || ab == NULL) {
		pl_state = PLAYER_ERROR;
		goto out;
	}
	while (!pl_quit) {
		struct request r = pl_req_get();

		if (r.seq == done) {
			k_msleep(5);
			continue;
		}
		done = r.seq;
		pl_video_session(&r, raw, ab);
	}
out:
	free(raw);
	free(ab);
}


/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/g2d.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "api/bili_parse.h"
#include "ui/cover.h"
#include "net/http.h"
#include "libs/tjpgd/tjpgd.h"

#define SLOTS		CONFIG_BILI_COVER_CACHE
#define JPEG_MAX	(48 * 1024)
#define THREADS		CONFIG_BILI_COVER_THREADS
#define RETRIES		3
#define URL_LEN		160

enum slot_state {
	SLOT_FREE,
	SLOT_QUEUED,	/* waiting for the thread */
	SLOT_BUSY,	/* the thread works on it */
	SLOT_READY,
	SLOT_FAILED,
};

struct slot {
	enum slot_state state;
	char pic[URL_LEN];
	uint32_t *px;
	int refs;
	uint8_t tries;		/* fetches that failed on the network */
	uint32_t used;		/* tick of the last use, for the choice of what to throw out */
};

static struct slot slots[SLOTS];
static K_MUTEX_DEFINE(lock);
static K_SEM_DEFINE(work, 0, K_SEM_MAX_LIMIT);
static volatile uint32_t version;
static volatile bool paused;
static uint32_t fetched, failed, soft, fetch_ms, decode_ms, worst_ms;

static const struct device *const vdec = DEVICE_DT_GET(DT_NODELABEL(ve));
static K_THREAD_STACK_ARRAY_DEFINE(stacks, THREADS, 8192);
static struct k_thread threads[THREADS];

static struct slot *find(const char *pic)
{
	for (int i = 0; i < SLOTS; i++) {
		if (slots[i].state != SLOT_FREE && strcmp(slots[i].pic, pic) == 0) {
			return &slots[i];
		}
	}

	return NULL;
}

/* a slot for a new cover: a free one, else the least recently used one nobody holds */
static struct slot *take_slot(void)
{
	struct slot *best = NULL;

	for (int i = 0; i < SLOTS; i++) {
		struct slot *s = &slots[i];

		if (s->state == SLOT_FREE) {
			return s;
		}
		if ((s->state == SLOT_READY || s->state == SLOT_FAILED) && s->refs == 0 &&
		    (best == NULL || (int32_t)(best->used - s->used) > 0)) {
			best = s;
		}
	}
	if (best != NULL) {
		free(best->px);
		best->px = NULL;
		best->state = SLOT_FREE;
	}

	return best;
}

void cover_request(const char *pic)
{
	struct slot *s;

	if (pic == NULL || pic[0] == '\0' || strlen(pic) >= URL_LEN) {
		return;
	}
	k_mutex_lock(&lock, K_FOREVER);
	s = find(pic);
	if (s != NULL) {
		s->used = k_uptime_get_32();
	} else {
		s = take_slot();
		if (s != NULL) {
			memset(s, 0, sizeof(*s));
			strcpy(s->pic, pic);
			s->state = SLOT_QUEUED;
			s->used = k_uptime_get_32();
			k_sem_give(&work);
		}
	}
	k_mutex_unlock(&lock);
}

const uint32_t *cover_acquire(const char *pic)
{
	const uint32_t *px = NULL;
	struct slot *s;

	k_mutex_lock(&lock, K_FOREVER);
	s = find(pic);
	if (s != NULL && s->state == SLOT_READY) {
		s->refs++;
		s->used = k_uptime_get_32();
		px = s->px;
	}
	k_mutex_unlock(&lock);

	return px;
}

void cover_release(const char *pic)
{
	struct slot *s;

	k_mutex_lock(&lock, K_FOREVER);
	s = find(pic);
	if (s != NULL && s->refs > 0) {
		s->refs--;
	}
	k_mutex_unlock(&lock);
}

uint32_t cover_version(void)
{
	return version;
}

void cover_pause(bool pause)
{
	paused = pause;
	if (!pause) {
		k_sem_give(&work);
	}
}

int cover_in_use(void)
{
	int n = 0;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < SLOTS; i++) {
		n += slots[i].refs > 0;
	}
	k_mutex_unlock(&lock);

	return n;
}

void cover_flush(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < SLOTS; i++) {
		struct slot *s = &slots[i];

		if (s->refs == 0 && s->state != SLOT_BUSY) {
			free(s->px);
			memset(s, 0, sizeof(*s));
		}
	}
	k_mutex_unlock(&lock);
	version++;
}

const uint32_t *cover_peek(int n)
{
	const uint32_t *px = NULL;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < SLOTS; i++) {
		if (slots[i].state == SLOT_READY && n-- == 0) {
			px = slots[i].px;
			break;
		}
	}
	k_mutex_unlock(&lock);

	return px;
}

/* covers that are asked for and not there yet (waiting or being fetched) */
int cover_pending(void)
{
	int n = 0;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < SLOTS; i++) {
		n += slots[i].state == SLOT_QUEUED || slots[i].state == SLOT_BUSY;
	}
	k_mutex_unlock(&lock);

	return n;
}

void cover_stats(char *buf, unsigned int size)
{
	int ready = 0, queued = 0, held = 0;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < SLOTS; i++) {
		ready += slots[i].state == SLOT_READY;
		queued += slots[i].state == SLOT_QUEUED || slots[i].state == SLOT_BUSY;
		held += slots[i].refs > 0;
	}
	k_mutex_unlock(&lock);
	snprintf(buf, size, "covers: %d of %d ready, %d queued, %d in use, %u fetched (%u in software), %u failed, "
		 "last: fetch %u ms + decode %u ms, worst %u ms", ready, SLOTS, queued, held, fetched, soft,
		 failed, fetch_ms, decode_ms, worst_ms);
}

static inline uint8_t clamp8(int v)
{
	return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* NV12 (full range, BT.601) to XRGB8888 at the cover size, nearest neighbour when the sizes differ */
static void to_xrgb_c(const struct vdec_frame *f, uint32_t *out)
{
	for (int y = 0; y < COVER_H; y++) {
		int sy = y * f->height / COVER_H;
		const uint8_t *yrow = f->plane[0] + sy * f->stride[0];
		const uint8_t *uvrow = f->plane[1] + (sy / 2) * f->stride[1];

		for (int x = 0; x < COVER_W; x++) {
			int sx = x * f->width / COVER_W;
			int Y = yrow[sx], U = uvrow[sx & ~1] - 128, V = uvrow[(sx & ~1) + 1] - 128;
			uint8_t r = clamp8(Y + ((359 * V) >> 8));
			uint8_t g = clamp8(Y - ((88 * U + 183 * V) >> 8));
			uint8_t b = clamp8(Y + ((454 * U) >> 8));

			out[y * (COVER_STRIDE / 4) + x] = 0xff000000U | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
		}
	}
}

/*
 * The picture of the video engine is NV12; the interface draws XRGB8888. The 2D accelerator reads
 * the YCbCr picture, converts it and scales it to the size of the cover, which keeps the processor
 * out of it (and the interface from converting RGB565 to its own format at every redraw).
 */
static uint32_t g2d_done, g2d_failed;

static void to_xrgb(const struct vdec_frame *f, uint32_t *out)
{
#ifdef CONFIG_BILI_COVER_G2D
	static const struct device *const g2d = DEVICE_DT_GET(DT_NODELABEL(g2d));

	if (device_is_ready(g2d)) {
		struct g2d_surface src = {
			.format = G2D_PIXFMT_NV12, .width = f->width, .height = f->height,
			.plane = {(void *)f->plane[0], (void *)f->plane[1]}, .pitch = {f->stride[0], f->stride[1]},
		};
		struct g2d_surface dst = {
			.format = G2D_PIXFMT_XRGB8888, .width = COVER_STRIDE / 4, .height = COVER_H, .plane = {out},
			.pitch = {COVER_STRIDE},
		};
		struct g2d_rect sr = {0, 0, f->width, f->height}, dr = {0, 0, COVER_W, COVER_H};

		if (g2d_blit(g2d, &src, &sr, &dst, &dr, G2D_ROTATE_0, G2D_FLAG_YUV_FULL_RANGE) == 0) {
			g2d_done++;
			return;
		}
		g2d_failed++;
	}
#endif
	to_xrgb_c(f, out);
}

/* ---- software decoding for what the video engine refuses (4:4:4, gray, ...) ------------------ */

struct sw {
	const uint8_t *data;
	size_t len, pos;
	void *out;
	bool xrgb;		/* 32 bit pixels, else RGB565 */
	int w, h, stride;	/* of the picture, in pixels; the row distance in pixels */
};

static size_t sw_in(JDEC *jd, uint8_t *buf, size_t n)
{
	struct sw *s = jd->device;
	size_t r = MIN(n, s->len - s->pos);

	if (buf != NULL) {
		memcpy(buf, s->data + s->pos, r);
	}
	s->pos += r;

	return r;
}

static int sw_out(JDEC *jd, void *bitmap, JRECT *rect)
{
	struct sw *s = jd->device;
	const uint8_t *p = bitmap;

	for (int y = rect->top; y <= rect->bottom; y++) {
		for (int x = rect->left; x <= rect->right; x++, p += 3) {
			if (x < s->w && y < s->h) {
				/* the decoder of LVGL delivers the bytes of a pixel as blue, green, red */
				if (s->xrgb) {
					((uint32_t *)s->out)[y * s->stride + x] =
						0xff000000U | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
				} else {
					((uint16_t *)s->out)[y * s->stride + x] =
						((p[2] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[0] >> 3);
				}
			}
		}
	}

	return 1;
}

static void *decode_sized(const uint8_t *jpeg, size_t n, int w, int h, int stride_bytes, bool xrgb)
{
	enum { POOL = 24 * 1024 };
	JDEC *jd = malloc(sizeof(*jd));
	void *pool = malloc(POOL);
	void *px = aligned_alloc(64, (size_t)stride_bytes * h);
	struct sw s = {.data = jpeg, .len = n, .out = px, .xrgb = xrgb, .w = w, .h = h,
		       .stride = stride_bytes / (xrgb ? 4 : 2)};

	if (px != NULL) {
		memset(px, 0, (size_t)stride_bytes * h);
	}
	if (jd == NULL || pool == NULL || px == NULL ||
	    jd_prepare(jd, sw_in, pool, POOL, &s) != JDR_OK || jd_decomp(jd, sw_out, 0) != JDR_OK) {
		free(px);
		px = NULL;
	}
	free(jd);
	free(pool);

	return px;
}

static uint32_t *decode_soft(const uint8_t *jpeg, size_t n)
{
	return decode_sized(jpeg, n, COVER_W, COVER_H, COVER_STRIDE, true);
}

uint16_t *cover_load_square(const char *pic, int size, int stride_bytes)
{
	char url[256];
	uint8_t *jpeg = malloc(JPEG_MAX);
	uint16_t *px = NULL;
	int n;

	if (jpeg == NULL || bili_cover_url(pic, size, size, url, sizeof(url)) < 0) {
		free(jpeg);
		return NULL;
	}
	n = http_get_text(url, &(struct http_opts){.range_start = -1, .timeout_ms = 8000}, (char *)jpeg, JPEG_MAX, NULL);
	if (n > 0) {
		px = decode_sized(jpeg, n, size, size, stride_bytes, false);
	}
	if (px == NULL) {
		printk("cover: the picture %s could not be loaded (%d)\n", pic, n);
	}
	free(jpeg);

	return px;
}

/*
 * True for what the video engine decodes: a baseline JPEG with three components in 4:2:0. Anything
 * else is not offered to it at all; a refused picture leaves the engine in a state that later
 * stopped the video decoder (the first picture came out, then nothing moved).
 */
static bool jpeg_for_engine(const uint8_t *d, size_t n)
{
	size_t i = 2;

	if (n < 4 || d[0] != 0xff || d[1] != 0xd8) {
		return false;
	}
	while (i + 9 < n && d[i] == 0xff) {
		uint8_t m = d[i + 1];
		size_t len = ((size_t)d[i + 2] << 8) | d[i + 3];

		if (m == 0xc0 || m == 0xc1) {
			/* length, precision, height(2), width(2), components, then id/sampling/table x3 */
			return d[i + 4] == 8 && d[i + 9] == 3 && d[i + 11] == 0x22 && d[i + 14] == 0x11 &&
			       d[i + 17] == 0x11;
		}
		if (m == 0xc2 || (m >= 0xc3 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc)) {
			return false;	/* progressive, lossless, arithmetic */
		}
		i += 2 + len;
	}

	return false;
}

/* fetches and decodes one cover; the pixels or NULL */
static uint32_t *load(const char *pic, uint8_t *jpeg, uint32_t *fetch_t, uint32_t *decode_t)
{
	char url[256];
	struct vdec_frame f;
	uint32_t *px = NULL;
	uint32_t t0 = k_uptime_get_32(), t1;
	int n, ret;

	if (bili_cover_url(pic, COVER_W, COVER_H, url, sizeof(url)) < 0) {
		return NULL;
	}
	n = http_get_text(url, &(struct http_opts){.range_start = -1, .timeout_ms = 8000}, (char *)jpeg,
			  JPEG_MAX, NULL);
	t1 = k_uptime_get_32();
	*fetch_t = t1 - t0;
	if (n <= 0) {
		printk("cover: %s fetch failed (%d)\n", pic, n);
		return NULL;
	}
	ret = jpeg_for_engine(jpeg, n) ? vdec_decode_image(vdec, VDEC_CODEC_JPEG, jpeg, n, VDEC_FORMAT_NV12, &f)
				       : -ENOTSUP;
	if (ret == 0) {
		px = aligned_alloc(64, COVER_BYTES);
		if (px != NULL) {
			to_xrgb(&f, px);
		}
		vdec_frame_release(vdec, &f);
	} else if (ret == -ENOTSUP) {
		/* the engine takes only baseline 4:2:0; the image server sends other kinds now and then */
		px = decode_soft(jpeg, n);
		if (px != NULL) {
			k_mutex_lock(&lock, K_FOREVER);
			soft++;
			k_mutex_unlock(&lock);
		}
	}
	if (px == NULL) {
		printk("cover: %s decode failed (%d, %d bytes)\n", pic, ret, n);
	}
	*decode_t = k_uptime_get_32() - t1;

	return px;
}

/* the oldest queued slot, marked busy; NULL when there is none */
static struct slot *next_job(void)
{
	struct slot *job = NULL;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < SLOTS; i++) {
		struct slot *s = &slots[i];

		/* the newest request first: it is the one on the screen */
		if (s->state == SLOT_QUEUED && (job == NULL || (int32_t)(s->used - job->used) > 0)) {
			job = s;
		}
	}
	if (job != NULL) {
		job->state = SLOT_BUSY;
	}
	k_mutex_unlock(&lock);

	return job;
}

static void cover_main(void *a, void *b, void *c)
{
	uint8_t *jpeg = malloc(JPEG_MAX);

	if (jpeg == NULL) {
		printk("cover: no memory\n");
		return;
	}
	for (;;) {
		struct slot *job;
		uint32_t *px;
		char pic[URL_LEN];
		uint32_t ft = 0, dt = 0;
		bool retry = false;

		k_sem_take(&work, K_FOREVER);
		while (paused) {
			k_msleep(200);
		}
		job = next_job();
		if (job == NULL) {
			continue;
		}
		strcpy(pic, job->pic);
		px = load(pic, jpeg, &ft, &dt);

		k_mutex_lock(&lock, K_FOREVER);
		job->px = px;
		if (px == NULL && dt == 0 && job->tries < RETRIES) {
			/* the fetch itself failed: another attempt after a pause (decode errors are final) */
			job->tries++;
			job->state = SLOT_QUEUED;
			retry = true;
		} else {
			job->state = px != NULL ? SLOT_READY : SLOT_FAILED;
			fetched += px != NULL;
			failed += px == NULL;
		}
		fetch_ms = ft;
		decode_ms = dt;
		worst_ms = MAX(worst_ms, ft + dt);
		k_mutex_unlock(&lock);
		if (retry) {
			k_msleep(300);
			k_sem_give(&work);
		} else {
			version++;
		}
	}
}

void cover_init(void)
{
	for (int i = 0; i < THREADS; i++) {
		k_thread_create(&threads[i], stacks[i], K_THREAD_STACK_SIZEOF(stacks[i]), cover_main, NULL, NULL,
				NULL, 9, 0, K_NO_WAIT);
		k_thread_name_set(&threads[i], "cover");
	}
}

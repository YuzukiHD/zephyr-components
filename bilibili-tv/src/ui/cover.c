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
#include <zephyr/drivers/vdec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "api/bili_parse.h"
#include "ui/cover.h"
#include "net/http.h"
#include "libs/tjpgd/tjpgd.h"
#ifdef CONFIG_BILI_GFX_RVV
#include "gfx/yuv_rvv.h"
#endif

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
	uint16_t *px;
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

const uint16_t *cover_acquire(const char *pic)
{
	const uint16_t *px = NULL;
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

const uint16_t *cover_peek(int n)
{
	const uint16_t *px = NULL;

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

/* NV12 (full range, BT.601) to RGB565 at the cover size, nearest neighbour when the sizes differ */
static void to_rgb565_c(const struct vdec_frame *f, uint16_t *out)
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

			out[y * (COVER_STRIDE / 2) + x] = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
		}
	}
}

#ifdef CONFIG_BILI_GFX_RVV
static bool yuv_vector_ok;

/* the vector conversion has to give the same pixels as the scalar one, checked once on noise */
static void yuv_vector_check(void)
{
	enum { W = COVER_W, H = COVER_H };
	uint8_t *y = malloc(W * H * 3 / 2);
	uint16_t *a = aligned_alloc(64, COVER_BYTES), *b = aligned_alloc(64, COVER_BYTES);
	uint32_t seed = 12345;
	bool same = false;

	if (y != NULL && a != NULL && b != NULL) {
		struct vdec_frame f = {.width = W, .height = H, .plane = {y, y + W * H}, .stride = {W, W}};

		for (int i = 0; i < W * H * 3 / 2; i++) {
			seed = seed * 1664525U + 1013904223U;
			y[i] = seed >> 24;
		}
		to_rgb565_c(&f, a);
		yuv_nv12_to_rgb565_rvv(y, W, y + W * H, W, b, COVER_STRIDE / 2, W, H);
		same = true;
		for (int r = 0; r < H && same; r++) {
			same = memcmp(a + r * (COVER_STRIDE / 2), b + r * (COVER_STRIDE / 2), W * 2) == 0;
		}
	}
	free(y);
	free(a);
	free(b);
	yuv_vector_ok = same;
	printk("cover: vector colour conversion %s\n", same ? "matches the scalar one" : "DIFFERS, not used");
}
#endif

static void to_rgb565(const struct vdec_frame *f, uint16_t *out)
{
#ifdef CONFIG_BILI_GFX_RVV
	if (yuv_vector_ok && f->width == COVER_W && f->height == COVER_H) {
		yuv_nv12_to_rgb565_rvv(f->plane[0], f->stride[0], f->plane[1], f->stride[1], out, COVER_STRIDE / 2,
				       COVER_W, COVER_H);
		return;
	}
#endif
	to_rgb565_c(f, out);
}

/* ---- software decoding for what the video engine refuses (4:4:4, gray, ...) ------------------ */

struct sw {
	const uint8_t *data;
	size_t len, pos;
	uint16_t *out;
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
				s->out[y * s->stride + x] = ((p[2] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[0] >> 3);
			}
		}
	}

	return 1;
}

static uint16_t *decode_sized(const uint8_t *jpeg, size_t n, int w, int h, int stride_bytes)
{
	enum { POOL = 24 * 1024 };
	JDEC *jd = malloc(sizeof(*jd));
	void *pool = malloc(POOL);
	uint16_t *px = aligned_alloc(64, (size_t)stride_bytes * h);
	struct sw s = {.data = jpeg, .len = n, .out = px, .w = w, .h = h, .stride = stride_bytes / 2};

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

static uint16_t *decode_soft(const uint8_t *jpeg, size_t n)
{
	return decode_sized(jpeg, n, COVER_W, COVER_H, COVER_STRIDE);
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
		px = decode_sized(jpeg, n, size, size, stride_bytes);
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
static uint16_t *load(const char *pic, uint8_t *jpeg, uint32_t *fetch_t, uint32_t *decode_t)
{
	char url[256];
	struct vdec_frame f;
	uint16_t *px = NULL;
	uint32_t t0 = k_uptime_get_32(), t1;
	int n, ret;
#ifdef CONFIG_BILI_COVER_CHECK
	static const int sample_x[6] = {30, 120, 200, 60, 150, 220}, sample_y[6] = {20, 60, 100, 110, 30, 120};
	uint8_t samp[6][3] = {0};
	int fw = 0, fh = 0, fs0 = 0;
#endif

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
			to_rgb565(&f, px);
		}
#ifdef CONFIG_BILI_COVER_CHECK
		for (int k = 0; k < 6; k++) {
			int sx = sample_x[k], sy = sample_y[k];
			const uint8_t *uvp = f.plane[1] + (sy / 2) * f.stride[1] + (sx & ~1);

			samp[k][0] = f.plane[0][sy * f.stride[0] + sx];
			samp[k][1] = uvp[0];
			samp[k][2] = uvp[1];
		}
		fw = f.width;
		fh = f.height;
		fs0 = f.stride[0];
#endif
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
#ifdef CONFIG_BILI_COVER_CHECK
	if (px != NULL && ret == 0) {
		/* the same picture decoded in software: a large difference means the engine's colours are wrong */
		uint16_t *ref = decode_soft(jpeg, n);

		if (ref != NULL) {
			uint32_t diff = 0, uvhw = 0;
			int rs = 0, bs = 0;

			for (int y = 0; y < COVER_H; y += 4) {
				for (int x = 0; x < COVER_W; x += 4) {
					uint16_t a = px[y * (COVER_STRIDE / 2) + x], b = ref[y * (COVER_STRIDE / 2) + x];
					int dr = (int)(a >> 11) - (int)(b >> 11), db = (int)(a & 31) - (int)(b & 31);

					diff += abs(dr) + abs(db);
					rs += dr;
					bs += db;
					uvhw++;
				}
			}
			if (diff / uvhw > 3) {
				printk("cover: %s engine and software differ: mean %u, red %d blue %d (sum), %d bytes, frame %dx%d stride %d\n",
				       pic, (unsigned)(diff / uvhw), rs, bs, n, fw, fh, fs0);
				for (int k = 0; k < 6; k++) {
					uint16_t b = ref[sample_y[k] * (COVER_STRIDE / 2) + sample_x[k]];

					printk("cover:   (%d,%d) engine Y %u Cb/Cr bytes %u %u; software rgb565 %04x\n", sample_x[k],
					       sample_y[k], samp[k][0], samp[k][1], samp[k][2], b);
				}
			}
			free(ref);
		}
	}
#endif
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
		uint16_t *px;
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
#ifdef CONFIG_BILI_GFX_RVV
	yuv_vector_check();
#endif
	for (int i = 0; i < THREADS; i++) {
		k_thread_create(&threads[i], stacks[i], K_THREAD_STACK_SIZEOF(stacks[i]), cover_main, NULL, NULL,
				NULL, 9, 0, K_NO_WAIT);
		k_thread_name_set(&threads[i], "cover");
	}
}

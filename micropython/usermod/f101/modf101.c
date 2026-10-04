/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * MicroPython module "f101": chip id, the 2D accelerator and audio output.
 */

#include <errno.h>
#include <string.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objarray.h"
#include "py/binary.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/g2d.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/audio/codec.h>

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */

static mp_buffer_info_t get_buffer(mp_obj_t obj, mp_uint_t flags)
{
	mp_buffer_info_t bufinfo;

	mp_get_buffer_raise(obj, &bufinfo, flags);
	return bufinfo;
}

/* a rectangle (x, y, w, h) tuple, or the whole w x h area for None */
static struct g2d_rect get_rect(mp_obj_t obj, int w, int h)
{
	struct g2d_rect r = { 0, 0, w, h };

	if (obj != mp_const_none) {
		mp_obj_t *items;

		mp_obj_get_array_fixed_n(obj, 4, &items);
		r.x = mp_obj_get_int(items[0]);
		r.y = mp_obj_get_int(items[1]);
		r.width = mp_obj_get_int(items[2]);
		r.height = mp_obj_get_int(items[3]);
	}
	return r;
}

/* ------------------------------------------------------------------------ */
/* f101.unique_id()                                                          */

static mp_obj_t f101_unique_id(void)
{
	uint8_t id[16];
	ssize_t n = hwinfo_get_device_id(id, sizeof(id));

	if (n < 0) {
		mp_raise_OSError(-n);
	}
	return mp_obj_new_bytes(id, n);
}
static MP_DEFINE_CONST_FUN_OBJ_0(f101_unique_id_obj, f101_unique_id);

/* ------------------------------------------------------------------------ */
/* G2D                                                                       */

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(g2d))

static const struct device *g2d_device(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(g2d));

	if (!device_is_ready(dev)) {
		mp_raise_OSError(ENODEV);
	}
	return dev;
}

/* a packed surface in a buffer, checked for size */
static struct g2d_surface get_surface(mp_obj_t buf, mp_obj_t w, mp_obj_t h, mp_obj_t fmt,
				      mp_uint_t flags)
{
	mp_buffer_info_t b = get_buffer(buf, flags);
	struct g2d_surface s = {
		.format = mp_obj_get_int(fmt),
		.width = mp_obj_get_int(w),
		.height = mp_obj_get_int(h),
	};
	unsigned int bpp;

	if ((int)s.format < 0 || s.format >= G2D_PIXFMT_MAX) {
		mp_raise_ValueError(MP_ERROR_TEXT("bad pixel format"));
	}
	bpp = g2d_format_bytes_per_pixel(s.format);
	if (bpp == 0) {
		mp_raise_ValueError(MP_ERROR_TEXT("planar format not supported"));
	}
	if (b.len < (size_t)s.width * s.height * bpp) {
		mp_raise_ValueError(MP_ERROR_TEXT("buffer too small"));
	}
	s.plane[0] = b.buf;
	return s;
}

static void g2d_check(int ret)
{
	if (ret != 0) {
		mp_raise_OSError(-ret);
	}
}

/* f101.g2d_fill(dst, width, height, format, color, rect=None) */
static mp_obj_t f101_g2d_fill(size_t n_args, const mp_obj_t *args)
{
	struct g2d_surface dst = get_surface(args[0], args[1], args[2], args[3], MP_BUFFER_RW);
	struct g2d_rect rect = get_rect(n_args > 5 ? args[5] : mp_const_none, dst.width, dst.height);

	g2d_check(g2d_fill(g2d_device(), &dst, &rect, mp_obj_get_int_truncated(args[4])));
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(f101_g2d_fill_obj, 5, 6, f101_g2d_fill);

/*
 * f101.g2d_blit(src, sw, sh, sfmt, dst, dw, dh, dfmt,
 *               src_rect=None, dst_rect=None, rotate=0, flags=0)
 */
static mp_obj_t f101_g2d_blit(size_t n_args, const mp_obj_t *args)
{
	struct g2d_surface src = get_surface(args[0], args[1], args[2], args[3], MP_BUFFER_READ);
	struct g2d_surface dst = get_surface(args[4], args[5], args[6], args[7], MP_BUFFER_RW);
	struct g2d_rect sr = get_rect(n_args > 8 ? args[8] : mp_const_none, src.width, src.height);
	struct g2d_rect dr = get_rect(n_args > 9 ? args[9] : mp_const_none, dst.width, dst.height);
	int rotate = n_args > 10 ? mp_obj_get_int(args[10]) : 0;
	uint32_t flags = n_args > 11 ? mp_obj_get_int(args[11]) : 0;

	g2d_check(g2d_blit(g2d_device(), &src, &sr, &dst, &dr, rotate / 90, flags));
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(f101_g2d_blit_obj, 8, 12, f101_g2d_blit);

/*
 * f101.g2d_blend(fg, fw, fh, ffmt, dst, dw, dh, dfmt, alpha=255, fg_rect=None, dst_rect=None)
 *
 * Source-over of fg on dst, in place.  alpha < 255 multiplies the pixel alpha
 * of the foreground.
 */
static mp_obj_t f101_g2d_blend(size_t n_args, const mp_obj_t *args)
{
	struct g2d_surface fg = get_surface(args[0], args[1], args[2], args[3], MP_BUFFER_READ);
	struct g2d_surface dst = get_surface(args[4], args[5], args[6], args[7], MP_BUFFER_RW);
	int alpha = n_args > 8 ? mp_obj_get_int(args[8]) : 255;
	struct g2d_rect fr = get_rect(n_args > 9 ? args[9] : mp_const_none, fg.width, fg.height);
	struct g2d_rect dr = get_rect(n_args > 10 ? args[10] : mp_const_none, dst.width, dst.height);
	struct g2d_blend blend = {
		.mode = G2D_BLEND_SRC_OVER,
		.fg_alpha_mode = alpha == 255 ? G2D_ALPHA_PIXEL : G2D_ALPHA_MIXED,
		.fg_alpha = alpha,
		.bg_alpha_mode = G2D_ALPHA_PIXEL,
	};

	g2d_check(g2d_blend(g2d_device(), &fg, &fr, &dst, &dr, &dst, &dr, &blend, 0));
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(f101_g2d_blend_obj, 8, 11, f101_g2d_blend);

#define F101_G2D_ENTRIES \
	{ MP_ROM_QSTR(MP_QSTR_g2d_fill), MP_ROM_PTR(&f101_g2d_fill_obj) }, \
	{ MP_ROM_QSTR(MP_QSTR_g2d_blit), MP_ROM_PTR(&f101_g2d_blit_obj) }, \
	{ MP_ROM_QSTR(MP_QSTR_g2d_blend), MP_ROM_PTR(&f101_g2d_blend_obj) }, \
	{ MP_ROM_QSTR(MP_QSTR_ARGB8888), MP_ROM_INT(G2D_PIXFMT_ARGB8888) }, \
	{ MP_ROM_QSTR(MP_QSTR_ABGR8888), MP_ROM_INT(G2D_PIXFMT_ABGR8888) }, \
	{ MP_ROM_QSTR(MP_QSTR_RGBA8888), MP_ROM_INT(G2D_PIXFMT_RGBA8888) }, \
	{ MP_ROM_QSTR(MP_QSTR_BGRA8888), MP_ROM_INT(G2D_PIXFMT_BGRA8888) }, \
	{ MP_ROM_QSTR(MP_QSTR_XRGB8888), MP_ROM_INT(G2D_PIXFMT_XRGB8888) }, \
	{ MP_ROM_QSTR(MP_QSTR_XBGR8888), MP_ROM_INT(G2D_PIXFMT_XBGR8888) }, \
	{ MP_ROM_QSTR(MP_QSTR_RGB888), MP_ROM_INT(G2D_PIXFMT_RGB888) }, \
	{ MP_ROM_QSTR(MP_QSTR_BGR888), MP_ROM_INT(G2D_PIXFMT_BGR888) }, \
	{ MP_ROM_QSTR(MP_QSTR_RGB565), MP_ROM_INT(G2D_PIXFMT_RGB565) }, \
	{ MP_ROM_QSTR(MP_QSTR_BGR565), MP_ROM_INT(G2D_PIXFMT_BGR565) }, \
	{ MP_ROM_QSTR(MP_QSTR_FLIP_H), MP_ROM_INT(G2D_FLIP_H) }, \
	{ MP_ROM_QSTR(MP_QSTR_FLIP_V), MP_ROM_INT(G2D_FLIP_V) },

#else
#define F101_G2D_ENTRIES
#endif /* g2d */

/* ------------------------------------------------------------------------ */
/* Audio: S16 stereo output through the on-chip codec                        */

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(audio_codec)) && \
	DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(codec_analog))

#define AUDIO_MAX_RATE		48000
#define AUDIO_BLOCK_MAX		(AUDIO_MAX_RATE / 50 * 4)	/* 20 ms of stereo S16 */
#define AUDIO_BLOCKS		6
#define AUDIO_PREFILL		3

K_MEM_SLAB_DEFINE_STATIC(audio_slab, AUDIO_BLOCK_MAX, AUDIO_BLOCKS, 4);

static const struct device *const audio_i2s = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
static const struct device *const audio_analog = DEVICE_DT_GET(DT_NODELABEL(codec_analog));

typedef struct _f101_audio_obj_t {
	mp_obj_base_t base;
	uint32_t rate;
	uint32_t block;		/* bytes per block */
	uint32_t queued;	/* blocks written since the start */
	bool running;
} f101_audio_obj_t;

static const mp_obj_type_t f101_audio_type;
static f101_audio_obj_t *audio_open_obj;

static void audio_check(int ret)
{
	if (ret != 0) {
		mp_raise_OSError(-ret);
	}
}

/* Audio(rate=48000): 44100 and 48000 Hz families */
static mp_obj_t f101_audio_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw,
				    const mp_obj_t *all_args)
{
	enum { ARG_rate };
	static const mp_arg_t allowed[] = {
		{ MP_QSTR_rate, MP_ARG_INT, { .u_int = 48000 } },
	};
	mp_arg_val_t args[MP_ARRAY_SIZE(allowed)];
	f101_audio_obj_t *self;
	struct i2s_config cfg;

	mp_arg_parse_all_kw_array(n_args, n_kw, all_args, MP_ARRAY_SIZE(allowed), allowed, args);
	if (audio_open_obj != NULL) {
		mp_raise_OSError(EBUSY);
	}
	if (!device_is_ready(audio_i2s) || !device_is_ready(audio_analog)) {
		mp_raise_OSError(ENODEV);
	}

	self = mp_obj_malloc(f101_audio_obj_t, type);
	self->rate = args[ARG_rate].u_int;
	self->block = self->rate / 50 * 4;
	if (self->rate < 8000 || self->rate > AUDIO_MAX_RATE || self->block > AUDIO_BLOCK_MAX) {
		mp_raise_ValueError(MP_ERROR_TEXT("rate"));
	}

	cfg = (struct i2s_config){
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = self->rate,
		.mem_slab = &audio_slab,
		.block_size = self->block,
		.timeout = 1000,
	};
	audio_check(i2s_configure(audio_i2s, I2S_DIR_TX, &cfg));
	audio_codec_start_output(audio_analog);
	audio_open_obj = self;
	return MP_OBJ_FROM_PTR(self);
}

static void audio_queue(f101_audio_obj_t *self, const uint8_t *data, size_t len)
{
	while (len > 0) {
		size_t n = MIN(len, self->block);
		void *block;
		int ret;

		ret = k_mem_slab_alloc(&audio_slab, &block, K_MSEC(1000));
		if (ret != 0) {
			mp_raise_OSError(ETIMEDOUT);
		}
		memcpy(block, data, n);
		if (n < self->block) {
			memset((uint8_t *)block + n, 0, self->block - n);
		}
		ret = i2s_write(audio_i2s, block, self->block);
		if (ret != 0) {
			k_mem_slab_free(&audio_slab, block);
			mp_raise_OSError(-ret);
		}
		self->queued++;
		if (!self->running && self->queued >= AUDIO_PREFILL) {
			audio_check(i2s_trigger(audio_i2s, I2S_DIR_TX, I2S_TRIGGER_START));
			self->running = true;
		}
		data += n;
		len -= n;
	}
}

/* write(buf): S16LE stereo frames; returns when everything is queued */
static mp_obj_t f101_audio_write(mp_obj_t self_in, mp_obj_t buf)
{
	f101_audio_obj_t *self = MP_OBJ_TO_PTR(self_in);
	mp_buffer_info_t b = get_buffer(buf, MP_BUFFER_READ);

	if (audio_open_obj != self) {
		mp_raise_OSError(EBADF);
	}
	audio_queue(self, b.buf, b.len);
	return mp_obj_new_int(b.len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(f101_audio_write_obj, f101_audio_write);

/* volume(0..255) */
static mp_obj_t f101_audio_volume(mp_obj_t self_in, mp_obj_t vol)
{
	audio_property_value_t val = { .vol = mp_obj_get_int(vol) };

	(void)self_in;
	audio_check(audio_codec_set_property(audio_analog, AUDIO_PROPERTY_OUTPUT_VOLUME,
					     AUDIO_CHANNEL_ALL, val));
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(f101_audio_volume_obj, f101_audio_volume);

/* tone(freq, ms, amplitude=12000): blocking square-ish sine burst */
static mp_obj_t f101_audio_tone(size_t n_args, const mp_obj_t *args)
{
	f101_audio_obj_t *self = MP_OBJ_TO_PTR(args[0]);
	int freq = mp_obj_get_int(args[1]);
	int ms = mp_obj_get_int(args[2]);
	int amp = n_args > 3 ? mp_obj_get_int(args[3]) : 12000;
	uint32_t frames = self->rate / 1000 * ms;
	uint32_t block_frames = self->block / 4;
	int16_t *buf;

	if (freq <= 0 || freq > (int)self->rate / 2) {
		mp_raise_ValueError(MP_ERROR_TEXT("freq"));
	}
	buf = m_malloc(self->block);
	for (uint32_t done = 0, phase = 0; done < frames; done += block_frames) {
		uint32_t n = MIN(block_frames, frames - done);

		for (uint32_t i = 0; i < n; i++) {
			/* Bhaskara's sine approximation, 0.2 percent */
			uint32_t pos = (uint32_t)(((uint64_t)phase * freq) % self->rate);
			float x = (float)(pos % (self->rate / 2 + 1)) / (self->rate / 2) * 3.14159265f;
			float y = 16.0f * x * (3.14159265f - x) /
				  (5.0f * 3.14159265f * 3.14159265f - 4.0f * x * (3.14159265f - x));

			buf[2 * i] = buf[2 * i + 1] =
				(int16_t)(amp * (pos < self->rate / 2 ? y : -y));
			phase++;
		}
		audio_queue(self, (uint8_t *)buf, n * 4);
	}
	m_free(buf);
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(f101_audio_tone_obj, 3, 4, f101_audio_tone);

static mp_obj_t f101_audio_close(mp_obj_t self_in)
{
	f101_audio_obj_t *self = MP_OBJ_TO_PTR(self_in);

	if (audio_open_obj == self) {
		if (self->running) {
			i2s_trigger(audio_i2s, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
			/* the drain returns at once, let the queued blocks play (20 ms each) */
			k_msleep(AUDIO_BLOCKS * 20 + 20);
		} else {
			i2s_trigger(audio_i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
		}
		audio_codec_stop_output(audio_analog);
		audio_open_obj = NULL;
		self->running = false;
	}
	return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(f101_audio_close_obj, f101_audio_close);

static const mp_rom_map_elem_t f101_audio_locals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&f101_audio_write_obj) },
	{ MP_ROM_QSTR(MP_QSTR_tone), MP_ROM_PTR(&f101_audio_tone_obj) },
	{ MP_ROM_QSTR(MP_QSTR_volume), MP_ROM_PTR(&f101_audio_volume_obj) },
	{ MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&f101_audio_close_obj) },
	{ MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&f101_audio_close_obj) },
};
static MP_DEFINE_CONST_DICT(f101_audio_locals_dict, f101_audio_locals_table);

static MP_DEFINE_CONST_OBJ_TYPE(f101_audio_type, MP_QSTR_Audio, MP_TYPE_FLAG_NONE,
				make_new, f101_audio_make_new,
				locals_dict, &f101_audio_locals_dict);

#define F101_AUDIO_ENTRIES \
	{ MP_ROM_QSTR(MP_QSTR_Audio), MP_ROM_PTR(&f101_audio_type) },
#else
#define F101_AUDIO_ENTRIES
#endif /* audio */

/* ------------------------------------------------------------------------ */

static const mp_rom_map_elem_t f101_module_globals_table[] = {
	{ MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_f101) },
	{ MP_ROM_QSTR(MP_QSTR_unique_id), MP_ROM_PTR(&f101_unique_id_obj) },
	F101_G2D_ENTRIES
	F101_AUDIO_ENTRIES
};
static MP_DEFINE_CONST_DICT(f101_module_globals, f101_module_globals_table);

const mp_obj_module_t f101_user_cmodule = {
	.base = { &mp_type_module },
	.globals = (mp_obj_dict_t *)&f101_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_f101, f101_user_cmodule);

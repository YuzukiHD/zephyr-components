/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The photo viewer: a JPEG is decoded by the video engine and shown on the video plane as it is,
 * a PNG is decoded to RGBA and converted to XRGB8888 for the video plane. The other photos of
 * the folder are reached with the buttons at the sides or a swipe; a tap hides the controls.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/fs/fs.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/fsutil.h"
#include "ui/ui.h"

#define MAX_PHOTOS	512
#define NAME_MAX_LEN	96
#define PATH_MAX_LEN	256
#define MAX_FILE_BYTES	(6 * 1024 * 1024)

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const vdec_dev = DEVICE_DT_GET(DT_NODELABEL(ve));

static char dir_path[PATH_MAX_LEN];
static char (*names)[NAME_MAX_LEN];
static int count, current;

/* what is on the video plane */
static bool have_frame, have_rgb;
static struct vdec_frame frame;
static uint32_t *rgb;

static lv_obj_t *top_bar, *title_label, *info_label, *message, *prev_btn, *next_btn;
static bool controls_visible = true;

static bool is_png(const char *name)
{
	return name_has_ext(name, ".png");
}

static bool ui_photo_ext(const char *name)
{
	return name_has_ext(name, ".jpg") || name_has_ext(name, ".jpeg") || name_has_ext(name, ".png");
}

static void names_free(void)
{
	free(names);
	names = NULL;
	count = 0;
}

/* the photos of the folder in name order */
static void names_load(void)
{
	struct fs_dir_t dir;
	struct fs_dirent d;

	names_free();
	names = malloc(MAX_PHOTOS * sizeof(*names));
	if (names == NULL) {
		return;
	}
	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, dir_path) != 0) {
		names_free();
		return;
	}
	while (fs_readdir(&dir, &d) == 0 && d.name[0] != '\0') {
		int at = count;

		if (d.type == FS_DIR_ENTRY_DIR || d.name[0] == '.' || count >= MAX_PHOTOS ||
		    strlen(d.name) >= NAME_MAX_LEN || !ui_photo_ext(d.name)) {
			continue;
		}
		while (at > 0 && name_cmp(d.name, names[at - 1]) < 0) {
			at--;
		}
		memmove(names[at + 1], names[at], (count - at) * sizeof(*names));
		strcpy(names[at], d.name);
		count++;
	}
	fs_closedir(&dir);
}

static void picture_drop(void)
{
	if (have_frame || have_rgb) {
		display_sunxi_hide_yuv(disp);
	}
	if (have_frame) {
		vdec_frame_release(vdec_dev, &frame);
		have_frame = false;
	}
	if (have_rgb) {
		free(rgb);
		rgb = NULL;
		have_rgb = false;
	}
}

static int file_read(const char *path, uint8_t **data, size_t *len)
{
	struct fs_dirent st;
	struct fs_file_t f;
	uint8_t *buf;
	size_t done = 0;
	int ret;

	ret = fs_stat(path, &st);
	if (ret < 0) {
		return ret;
	}
	if (st.size == 0 || st.size > MAX_FILE_BYTES) {
		return -EFBIG;
	}
	buf = malloc(st.size);
	if (buf == NULL) {
		return -ENOMEM;
	}
	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret < 0) {
		free(buf);
		return ret;
	}
	while (done < st.size) {
		ssize_t n = fs_read(&f, buf + done, MIN(st.size - done, 64 * 1024));

		if (n <= 0) {
			ret = n < 0 ? n : -EIO;
			break;
		}
		done += n;
	}
	fs_close(&f);
	if (done != st.size) {
		free(buf);
		return ret;
	}
	*data = buf;
	*len = st.size;

	return 0;
}

/* RGBA8888 as the decoder delivers it (bytes A, B, G, R) to XRGB8888, over black */
static int rgba_to_xrgb(const struct vdec_frame *f)
{
	size_t bytes = (size_t)f->width * f->height * 4;
	uint32_t *dst = aligned_alloc(64, ROUND_UP(bytes, 64));

	if (dst == NULL) {
		return -ENOMEM;
	}
	for (unsigned int y = 0; y < f->height; y++) {
		const uint8_t *s = f->plane[0] + (size_t)y * f->stride[0];
		uint32_t *d = dst + (size_t)y * f->width;

		for (unsigned int x = 0; x < f->width; x++, s += 4) {
			uint32_t a = s[0];

			d[x] = a == 255 ? ((uint32_t)s[3] << 16) | ((uint32_t)s[2] << 8) | s[1]
					: ((s[3] * a / 255) << 16) | ((s[2] * a / 255) << 8) |
					  (s[1] * a / 255);
		}
	}
	sys_cache_data_flush_range(dst, bytes);
	rgb = dst;
	have_rgb = true;

	return 0;
}

/* ---- zoom: two fingers pinch, one finger pans, a double tap toggles --------------------------- */

#define SCREEN_W	1024
#define SCREEN_H	768
#define TOUCH_SLOTS	5
#define ZOOM_MAX	8.0f
#define ZOOM_DOUBLE	3.0f
#define DOUBLE_TAP_MS	400
#define TAP_MS		250
#define TAP_SLOP	25
#define SUPPRESS_MS	300
#define VIEW_MS		33

static struct {
	int x, y;
	bool down;
} fingers[TOUCH_SLOTS];
static int cur_slot;
static volatile uint32_t suppress_until;
static volatile bool zoomed;

static uint16_t pic_w, pic_h;
static float zoom = 1.0f, view_cx, view_cy;	/* zoom 1: the picture fits the screen */
static bool view_dirty;
static lv_timer_t *view_timer;

static void touch_event(struct input_event *evt, void *user_data)
{
	int down = 0;

	ARG_UNUSED(user_data);
	switch (evt->code) {
	case INPUT_ABS_MT_SLOT:
		cur_slot = CLAMP(evt->value, 0, TOUCH_SLOTS - 1);
		return;
	case INPUT_ABS_X:
		fingers[cur_slot].x = evt->value;
		break;
	case INPUT_ABS_Y:
		fingers[cur_slot].y = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		fingers[cur_slot].down = evt->value != 0;
		break;
	default:
		return;
	}
	for (int i = 0; i < TOUCH_SLOTS; i++) {
		down += fingers[i].down;
	}
	/* the clicks and swipes the user interface derives from such a touch are not wanted */
	if (down >= 2 || (zoomed && down >= 1)) {
		suppress_until = k_uptime_get_32() + SUPPRESS_MS;
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), touch_event, NULL);

static bool suppressed(void)
{
	return (int32_t)(suppress_until - k_uptime_get_32()) > 0;
}

/* pixels of the screen per pixel of the picture */
static float scale_now(void)
{
	float fit = MIN((float)SCREEN_W / pic_w, (float)SCREEN_H / pic_h);

	return fit * zoom;
}

static void view_clamp(void)
{
	float s = scale_now();
	float w = MIN((float)pic_w, SCREEN_W / s), h = MIN((float)pic_h, SCREEN_H / s);

	zoom = CLAMP(zoom, 1.0f, ZOOM_MAX);
	view_cx = CLAMP(view_cx, w / 2, pic_w - w / 2);
	view_cy = CLAMP(view_cy, h / 2, pic_h - h / 2);
	zoomed = zoom > 1.001f;
}

/* shows the part of the picture that the view covers */
static void view_apply(void)
{
	float s = scale_now();
	float w = MIN((float)pic_w, SCREEN_W / s), h = MIN((float)pic_h, SCREEN_H / s);
	uint16_t cw = (uint16_t)w, ch = (uint16_t)h;
	uint16_t cx = (uint16_t)MAX(view_cx - w / 2, 0.0f), cy = (uint16_t)MAX(view_cy - h / 2, 0.0f);

	if (zoom <= 1.001f) {
		cx = cy = cw = ch = 0;
	}
	if (have_frame) {
		struct display_sunxi_yuv yuv = {
			.y = frame.plane[0],
			.uv = frame.plane[1],
			.width = frame.width,
			.height = frame.height,
			.stride_y = frame.stride[0],
			.stride_uv = frame.stride[1],
			.full_range = true,
			.crop_x = cx, .crop_y = cy, .crop_width = cw, .crop_height = ch,
			.nonblock = true,
		};

		display_sunxi_show_yuv(disp, &yuv);
	} else if (have_rgb) {
		struct display_sunxi_rgb img = {
			.data = rgb,
			.width = pic_w,
			.height = pic_h,
			.stride = pic_w * 4,
			.xrgb8888 = true,
			.crop_x = cx, .crop_y = cy, .crop_width = cw, .crop_height = ch,
			.nonblock = true,
		};

		display_sunxi_show_rgb(disp, &img);
	}
}

static void view_reset(void)
{
	zoom = 1.0f;
	zoomed = false;
	view_cx = pic_w / 2.0f;
	view_cy = pic_h / 2.0f;
	view_dirty = false;
}

/* zooms by @p factor keeping the picture point under the screen point (mx, my) in place */
static void view_zoom_at(float factor, float mx, float my)
{
	float s = scale_now();
	float px = view_cx + (mx - SCREEN_W / 2.0f) / s;
	float py = view_cy + (my - SCREEN_H / 2.0f) / s;

	zoom = CLAMP(zoom * factor, 1.0f, ZOOM_MAX);
	s = scale_now();
	view_cx = px - (mx - SCREEN_W / 2.0f) / s;
	view_cy = py - (my - SCREEN_H / 2.0f) / s;
	view_clamp();
	view_dirty = true;
}

static void view_tick(lv_timer_t *t)
{
	static int last_n;
	static float last_dist, last_mx, last_my, last_x, last_y;
	static uint32_t t_down, t_tap;
	static int down_x, down_y, max_n;
	static int tap_x, tap_y;
	int idx[2], n = 0;
	uint32_t now = k_uptime_get_32();

	ARG_UNUSED(t);
	if (pic_w == 0 || !(have_frame || have_rgb)) {
		return;
	}
	for (int i = 0; i < TOUCH_SLOTS && n < 2; i++) {
		if (fingers[i].down) {
			idx[n++] = i;
		}
	}
	if (n == 2) {
		float dx = fingers[idx[1]].x - fingers[idx[0]].x;
		float dy = fingers[idx[1]].y - fingers[idx[0]].y;
		float dist = sqrtf(dx * dx + dy * dy);
		float mx = (fingers[idx[0]].x + fingers[idx[1]].x) / 2.0f;
		float my = (fingers[idx[0]].y + fingers[idx[1]].y) / 2.0f;

		if (last_n == 2 && last_dist > 20.0f && dist > 20.0f) {
			float s;

			view_zoom_at(dist / last_dist, mx, my);
			/* the midpoint moving pans */
			s = scale_now();
			view_cx -= (mx - last_mx) / s;
			view_cy -= (my - last_my) / s;
			view_clamp();
			view_dirty = true;
		}
		last_dist = dist;
		last_mx = mx;
		last_my = my;
	} else if (n == 1 && zoomed) {
		float x = fingers[idx[0]].x, y = fingers[idx[0]].y;

		if (last_n == 1) {
			float s = scale_now();

			view_cx -= (x - last_x) / s;
			view_cy -= (y - last_y) / s;
			view_clamp();
			view_dirty = true;
		}
	}
	if (n >= 1) {
		last_x = fingers[idx[0]].x;
		last_y = fingers[idx[0]].y;
	}
	/* taps: a short touch with one finger that hardly moved */
	if (n >= 1 && last_n == 0) {
		t_down = now;
		down_x = fingers[idx[0]].x;
		down_y = fingers[idx[0]].y;
		max_n = 0;
	}
	max_n = MAX(max_n, n);
	if (n == 0 && last_n >= 1 && max_n == 1 && now - t_down < TAP_MS &&
	    abs((int)last_x - down_x) < TAP_SLOP && abs((int)last_y - down_y) < TAP_SLOP) {
		if (t_tap != 0 && now - t_tap < DOUBLE_TAP_MS && abs(down_x - tap_x) < 60 &&
		    abs(down_y - tap_y) < 60) {
			if (zoomed) {
				view_reset();
				view_dirty = true;
			} else {
				view_zoom_at(ZOOM_DOUBLE, down_x, down_y);
			}
			t_tap = 0;
		} else {
			t_tap = now;
			tap_x = down_x;
			tap_y = down_y;
		}
	}
	last_n = n;
	if (view_dirty) {
		view_dirty = false;
		view_apply();
		if (zoomed) {
			lv_label_set_text_fmt(info_label, "%d / %d   %ux%u   x%d.%d", current + 1, count,
					      pic_w, pic_h, (int)zoom, (int)(zoom * 10) % 10);
		} else {
			lv_label_set_text_fmt(info_label, "%d / %d   %ux%u", current + 1, count, pic_w,
					      pic_h);
		}
	}
}

static void message_show(const char *text)
{
	if (text == NULL) {
		lv_obj_add_flag(message, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_label_set_text(message, text);
		lv_obj_remove_flag(message, LV_OBJ_FLAG_HIDDEN);
	}
}

static void controls_show(bool show)
{
	controls_visible = show;
	/* with no controls the plane of the user interface is off and only the picture is read */
	ui_overlay_set(show || !(have_frame || have_rgb));
	if (show) {
		lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
		if (count > 1) {
			lv_obj_remove_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
			lv_obj_remove_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
		}
	} else {
		lv_obj_add_flag(top_bar, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
	}
}

static const char *error_text(int ret)
{
	switch (ret) {
	case -EFBIG:
		return "The file is too large";
	case -ENOMEM:
		return "The picture is too large for the memory";
	case -ENOTSUP:
		return "This kind of picture is not supported\n(JPEG: baseline 4:2:0)";
	default:
		return "Cannot open this picture";
	}
}

static void photo_show(int idx)
{
	char path[PATH_MAX_LEN];
	uint8_t *data;
	size_t len;
	int64_t t0 = k_uptime_get();
	int ret;

	current = idx;
	picture_drop();
	pic_w = pic_h = 0;
	lv_label_set_text(title_label, names[idx]);
	lv_label_set_text_fmt(info_label, "%d / %d", idx + 1, count);
	message_show(NULL);

	snprintf(path, sizeof(path), "%s/%s", dir_path, names[idx]);
	ret = file_read(path, &data, &len);
	if (ret == 0) {
		bool png = is_png(names[idx]);
		struct vdec_frame f;

		ret = vdec_decode_image(vdec_dev, png ? VDEC_CODEC_PNG : VDEC_CODEC_JPEG, data, len,
					png ? VDEC_FORMAT_RGBA8888 : VDEC_FORMAT_NV12, &f);
		free(data);
		if (ret == 0 && !png) {
			struct display_sunxi_yuv yuv = {
				.y = f.plane[0],
				.uv = f.plane[1],
				.width = f.width,
				.height = f.height,
				.stride_y = f.stride[0],
				.stride_uv = f.stride[1],
				.full_range = true,
			};

			frame = f;
			have_frame = true;
			pic_w = f.width;
			pic_h = f.height;
			view_reset();
			ret = display_sunxi_show_yuv(disp, &yuv);
			lv_label_set_text_fmt(info_label, "%d / %d   %ux%u", idx + 1, count, f.width,
					      f.height);
		} else if (ret == 0) {
			ret = rgba_to_xrgb(&f);
			if (ret == 0) {
				struct display_sunxi_rgb img = {
					.data = rgb,
					.width = f.width,
					.height = f.height,
					.stride = f.width * 4,
					.xrgb8888 = true,
				};

				ret = display_sunxi_show_rgb(disp, &img);
				pic_w = f.width;
				pic_h = f.height;
				view_reset();
			}
			lv_label_set_text_fmt(info_label, "%d / %d   %ux%u", idx + 1, count, f.width,
					      f.height);
			vdec_frame_release(vdec_dev, &f);
		}
	}
	if (ret != 0) {
		printk("photos: %s: %d\n", names[idx], ret);
		picture_drop();
		message_show(error_text(ret));
		ui_overlay_set(true);
	} else {
		printk("photos: %s shown in %d ms\n", names[idx], (int)(k_uptime_get() - t0));
		controls_show(controls_visible);
	}
}

static void step(int dir)
{
	if (count > 1) {
		photo_show((current + dir + count) % count);
	}
}

static void prev_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	step(-1);
}

static void next_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	step(1);
}

static void screen_clicked(lv_event_t *e)
{
	if (suppressed()) {
		return;
	}
	if (lv_event_get_target(e) == lv_event_get_current_target(e)) {
		controls_show(!controls_visible);
	}
}

static void screen_gesture(lv_event_t *e)
{
	lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

	ARG_UNUSED(e);
	if (suppressed() || zoomed) {
		return;
	}
	if (dir == LV_DIR_LEFT) {
		step(1);
	} else if (dir == LV_DIR_RIGHT) {
		step(-1);
	}
}

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto_arg("files", dir_path);
}

static lv_obj_t *side_button(lv_obj_t *scr, const char *symbol, lv_align_t align, lv_event_cb_t cb)
{
	lv_obj_t *b = ui_icon_button(scr, symbol, 72);

	lv_obj_set_style_bg_opa(b, LV_OPA_70, 0);
	lv_obj_align(b, align, align == LV_ALIGN_LEFT_MID ? 24 : -24, 0);
	lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

	return b;
}

static lv_obj_t *photos_create(void)
{
	lv_obj_t *scr = ui_screen_new(true);
	lv_obj_t *back;

	lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(scr, screen_clicked, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(scr, screen_gesture, LV_EVENT_GESTURE, NULL);

	top_bar = lv_obj_create(scr);
	lv_obj_remove_style_all(top_bar);
	lv_obj_set_size(top_bar, LV_PCT(100), 112);
	lv_obj_set_style_bg_color(top_bar, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(top_bar, LV_OPA_50, 0);
	lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
	back = ui_icon_button(top_bar, LV_SYMBOL_LEFT, 64);
	lv_obj_set_pos(back, 32, 24);
	lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
	title_label = lv_label_create(top_bar);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xE8ECF4), 0);
	lv_obj_set_width(title_label, 620);
	lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
	lv_obj_set_pos(title_label, 120, 38);
	info_label = lv_label_create(top_bar);
	lv_obj_set_style_text_font(info_label, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(info_label, lv_color_hex(0x8B95A8), 0);
	lv_obj_align(info_label, LV_ALIGN_TOP_RIGHT, -32, 40);

	prev_btn = side_button(scr, LV_SYMBOL_LEFT, LV_ALIGN_LEFT_MID, prev_clicked);
	next_btn = side_button(scr, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, next_clicked);

	message = lv_label_create(scr);
	lv_obj_set_style_text_font(message, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(message, lv_color_hex(0xE8ECF4), 0);
	lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(message, 700);
	lv_obj_align(message, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_flag(message, LV_OBJ_FLAG_HIDDEN);

	if (view_timer == NULL) {
		view_timer = lv_timer_create(view_tick, VIEW_MS, NULL);
		lv_timer_pause(view_timer);
	}

	return scr;
}

/* the argument is the path of the photo to show */
static void photos_enter(void *arg)
{
	const char *path = arg;
	const char *slash;

	if (path == NULL) {
		return;
	}
	slash = strrchr(path, '/');
	if (slash == NULL) {
		return;
	}
	snprintf(dir_path, sizeof(dir_path), "%.*s", (int)(slash - path), path);
	lv_timer_resume(view_timer);
	names_load();
	current = 0;
	for (int i = 0; i < count; i++) {
		if (strcmp(names[i], slash + 1) == 0) {
			current = i;
		}
	}
	controls_visible = true;
	if (count == 0) {
		message_show("No pictures in this folder");
		return;
	}
	if (count > 1) {
		lv_obj_remove_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(prev_btn, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(next_btn, LV_OBJ_FLAG_HIDDEN);
	}
	photo_show(current);
}

static void photos_leave(void)
{
	lv_timer_pause(view_timer);
	zoomed = false;
	picture_drop();
	names_free();
	ui_overlay_set(true);
}

MP5_SCREEN(photos, "photos", photos_create, photos_enter, photos_leave);

static void photo_open(const char *path)
{
	ui_goto_arg("photos", (void *)path);
}

static const char *const photo_exts[] = {".jpg", ".jpeg", ".png", NULL};

MP5_FILE_HANDLER(photo, .kind = "photo", .title = "Photos", .symbol = LV_SYMBOL_IMAGE,
		 .color = &ui_pal.ok, .exts = photo_exts, .open = photo_open);
MP5_HOME_TILE(photos, .order = 30, .symbol = LV_SYMBOL_IMAGE, .title = "Photos",
	      .subtitle = "JPEG and PNG", .color_top = 0x3DDC97, .color_bottom = 0x1FA97A,
	      .kind = "photo");

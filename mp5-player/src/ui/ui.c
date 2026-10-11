/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Shared pieces of the user interface: screen switching and the building blocks of the bars */

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/app.h"
#include "mp5.h"

static const struct screen_ops *screen_ops[SCR_COUNT];
static lv_obj_t *screens[SCR_COUNT];
static enum screen_id current = SCR_COUNT;
static const char *const screen_names[SCR_COUNT] = {
	[SCR_HOME] = "home", [SCR_FILES] = "files", [SCR_PHOTOS] = "photos",
	[SCR_PLAYER] = "player", [SCR_USB] = "usb", [SCR_RECENT] = "videos", [SCR_GAME] = "game", [SCR_SETTINGS] = "settings", [SCR_WIFI] = "wifi",
};

/* ---- theme ---------------------------------------------------------------------------- */

static const struct {
	const char *name;
	uint32_t accent, accent2;
} accents[UI_ACCENTS] = {
	{"Blue", 0x6C8CFF, 0x9B6CFF},
	{"Purple", 0x9B6CFF, 0xD443A8},
	{"Green", 0x3DDC97, 0x1FA97A},
	{"Orange", 0xFFB547, 0xF2822C},
	{"Pink", 0xFF6C9B, 0xD443A8},
	{"Red", 0xE5484D, 0xB02A37},
};

struct palette ui_pal = {
	.bg = 0x0B0F17, .surface = 0x151B28, .surface2 = 0x1F2740, .border = 0x2A3350,
	.text = 0xE8ECF4, .dim = 0x8B95A8, .accent = 0x6C8CFF, .accent2 = 0x9B6CFF,
	.ok = 0x3DDC97, .warn = 0xFFB547, .pink = 0xFF6C9B,
};
static bool theme_dark = true;
static int theme_accent;

bool ui_theme_dark(void)
{
	return theme_dark;
}

int ui_theme_accent(void)
{
	return theme_accent;
}

const char *ui_theme_accent_name(int index)
{
	return accents[CLAMP(index, 0, UI_ACCENTS - 1)].name;
}

uint32_t ui_theme_accent_color(int index)
{
	return accents[CLAMP(index, 0, UI_ACCENTS - 1)].accent;
}

static void palette_make(bool dark, int accent)
{
	theme_dark = dark;
	theme_accent = CLAMP(accent, 0, UI_ACCENTS - 1);
	if (dark) {
		ui_pal = (struct palette){
			.bg = 0x0B0F17, .surface = 0x151B28, .surface2 = 0x1F2740, .border = 0x2A3350,
			.text = 0xE8ECF4, .dim = 0x8B95A8,
			.ok = 0x3DDC97, .warn = 0xFFB547, .pink = 0xFF6C9B,
		};
	} else {
		ui_pal = (struct palette){
			.bg = 0xF1F3F9, .surface = 0xFFFFFF, .surface2 = 0xDFE5F2, .border = 0xCBD3E4,
			.text = 0x1B2233, .dim = 0x5C687F,
			.ok = 0x1FA97A, .warn = 0xD9830A, .pink = 0xE0457B,
		};
	}
	ui_pal.accent = accents[theme_accent].accent;
	ui_pal.accent2 = accents[theme_accent].accent2;
}

/* the saved theme, before the screens are built */
static void theme_load(void)
{
	const char *mode = cfg_get("theme");
	const char *acc = cfg_get("accent");

	palette_make(mode == NULL || strcmp(mode, "light") != 0, acc != NULL ? atoi(acc) : 0);
}

static int brightness = 255;

int ui_brightness(void)
{
	return brightness;
}

void ui_brightness_set(int value, bool save)
{
	static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	char buf[8];

	brightness = CLAMP(value, 10, 255);
	display_set_brightness(disp, brightness);
	if (save) {
		snprintf(buf, sizeof(buf), "%d", brightness);
		cfg_set("brightness", buf);
	}
}

static void ui_rebuild(void *unused);

void ui_theme_set(bool dark, int accent)
{
	char buf[8];

	palette_make(dark, accent);
	cfg_set("theme", dark ? "dark" : "light");
	snprintf(buf, sizeof(buf), "%d", theme_accent);
	cfg_set("accent", buf);
	/* the screen that asks is one of those to be deleted: do it after its event */
	lv_async_call(ui_rebuild, NULL);
}

lv_obj_t *ui_screen_new(bool transparent)
{
	lv_obj_t *s = lv_obj_create(NULL);

	lv_obj_remove_style_all(s);
	lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
	lv_obj_set_style_bg_color(s, lv_color_hex(C_BG), 0);
	lv_obj_set_style_bg_opa(s, transparent ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
	lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);

	return s;
}

lv_obj_t *ui_icon_button(lv_obj_t *parent, const char *symbol, int32_t size)
{
	lv_obj_t *b = lv_button_create(parent);
	lv_obj_t *l = lv_label_create(b);

	lv_obj_remove_style_all(b);
	lv_obj_set_size(b, size, size);
	lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(b, lv_color_hex(C_SURFACE2), 0);
	lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(b, lv_color_hex(C_ACCENT), LV_STATE_PRESSED);
	lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
	lv_obj_set_style_text_font(l, size >= 64 ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
	lv_label_set_text(l, symbol);
	lv_obj_center(l);

	return b;
}

lv_obj_t *ui_title_bar(lv_obj_t *parent, const char *title, lv_event_cb_t on_back)
{
	lv_obj_t *bar = lv_obj_create(parent);
	lv_obj_t *back = ui_icon_button(bar, LV_SYMBOL_LEFT, 56);
	lv_obj_t *t = lv_label_create(bar);

	lv_obj_remove_style_all(bar);
	lv_obj_set_size(bar, LV_PCT(100), 88);
	lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(bar, 20, 0);
	lv_obj_set_style_pad_hor(bar, 32, 0);
	lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
	lv_obj_set_style_text_color(t, lv_color_hex(C_TEXT), 0);
	lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
	lv_label_set_text(t, title);

	return bar;
}

void ui_register(enum screen_id id, const struct screen_ops *ops)
{
	screen_ops[id] = ops;
}

/*
 * A new screen is drawn band by band into the frame buffer the display engine is scanning out,
 * which shows as a wipe from the top. The plane is kept off until the whole screen is drawn.
 */
static void screen_drawn(lv_event_t *e)
{
	static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	lv_display_remove_event_cb_with_user_data(lv_event_get_target(e), screen_drawn, NULL);
	display_sunxi_fb_enable(disp, true);
}

void ui_goto_arg(enum screen_id id, void *arg)
{
	static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	if (id >= SCR_COUNT || screen_ops[id] == NULL) {
		return;
	}
	if (screens[id] == NULL) {
		screens[id] = screen_ops[id]->create();
	}
	if (current < SCR_COUNT && screen_ops[current]->leave != NULL) {
		screen_ops[current]->leave();
	}
	current = id;
	ui_stats_page_begin(screen_names[id]);
	{
		uint32_t c0 = k_cycle_get_32();

		if (screen_ops[id]->enter != NULL) {
			screen_ops[id]->enter(arg);
		}
		ui_stats_page_set_up(k_cyc_to_us_floor32(k_cycle_get_32() - c0));
	}
	display_sunxi_fb_enable(disp, false);
	lv_display_add_event_cb(lv_display_get_default(), screen_drawn, LV_EVENT_REFR_READY, NULL);
	lv_screen_load(screens[id]);
}

void ui_goto(enum screen_id id)
{
	ui_goto_arg(id, NULL);
}

/* every screen is made again with the new colours */
static void ui_rebuild(void *unused)
{
	enum screen_id cur = current;
	bool made[SCR_COUNT];
	lv_obj_t *tmp = lv_obj_create(NULL);

	ARG_UNUSED(unused);
	if (cur < SCR_COUNT && screen_ops[cur]->leave != NULL) {
		screen_ops[cur]->leave();
	}
	lv_screen_load(tmp);
	for (int i = 0; i < SCR_COUNT; i++) {
		made[i] = screens[i] != NULL;
		if (made[i]) {
			lv_obj_delete(screens[i]);
			screens[i] = NULL;
		}
	}
	/* the screens that have timers of their own must have their objects again at once */
	for (int i = 0; i < SCR_COUNT; i++) {
		if (made[i]) {
			screens[i] = screen_ops[i]->create();
		}
	}
	current = SCR_COUNT;
	ui_goto(cur < SCR_COUNT ? cur : SCR_HOME);
	lv_obj_delete_async(tmp);
}

static int ui_step(void)
{
	theme_load();
	{
		const char *b = cfg_get("brightness");

		if (b != NULL) {
			ui_brightness_set(atoi(b), false);
		}
	}
	ui_stats_init();
	ui_register(SCR_HOME, &home_ops);
	ui_register(SCR_FILES, &files_ops);
	ui_register(SCR_RECENT, &recent_ops);
	ui_register(SCR_GAME, &game_ops);
	ui_register(SCR_PHOTOS, &photos_ops);
	ui_register(SCR_USB, &usb_ops);
	ui_register(SCR_SETTINGS, &settings_ops);
	ui_register(SCR_WIFI, &wifi_ops);
	ui_register(SCR_PLAYER, &player_ops);
	ui_goto(SCR_HOME);

	return 0;
}
MP5_INIT(MP5_STAGE_UI, 20, "interface", ui_step);

/* the first picture: drawn, then the panel is switched on */
void ui_present(void)
{
	static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	lv_timer_handler();
	display_blanking_off(disp);
	printk("mp5: boot %u ms: first picture\n", k_uptime_get_32());
}

/* the user interface thread: how long a redraw takes shows in the log when it is slow */
void ui_loop(void)
{
	printk("mp5: %d x %d\n", (int)lv_display_get_horizontal_resolution(NULL),
	       (int)lv_display_get_vertical_resolution(NULL));
	for (;;) {
		static uint32_t slow, worst_us, report_ms;
		uint32_t t0 = k_cycle_get_32(), next, us;

		next = lv_timer_handler();
		us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);
		if (us > 30000U) {
			slow++;
		}
		worst_us = MAX(worst_us, us);
		if (k_uptime_get_32() - report_ms >= 5000U) {
			if (worst_us > 8000U) {
				printk("ui: worst redraw %u ms, %u slower than 30 ms\n", worst_us / 1000,
				       slow);
			}
			slow = worst_us = 0;
			report_ms = k_uptime_get_32();
		}
		k_msleep(MIN(next, 20U));
	}
}

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Shared pieces of the user interface: screen switching and the building blocks of the bars */

#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "mp5.h"

static const struct screen_ops *screen_ops[SCR_COUNT];
static lv_obj_t *screens[SCR_COUNT];
static enum screen_id current = SCR_COUNT;
static const char *const screen_names[SCR_COUNT] = {
	[SCR_HOME] = "home", [SCR_FILES] = "files", [SCR_PHOTOS] = "photos",
	[SCR_PLAYER] = "player", [SCR_USB] = "usb", [SCR_RECENT] = "videos", [SCR_GAME] = "game", [SCR_SETTINGS] = "settings",
};

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

void ui_init(void)
{
	ui_stats_init();
	ui_register(SCR_HOME, &home_ops);
	ui_register(SCR_FILES, &files_ops);
	ui_register(SCR_RECENT, &recent_ops);
	ui_register(SCR_GAME, &game_ops);
	ui_register(SCR_PHOTOS, &photos_ops);
	ui_register(SCR_SETTINGS, &settings_ops);
	ui_register(SCR_PLAYER, &player_ops);
	ui_goto(SCR_HOME);
}

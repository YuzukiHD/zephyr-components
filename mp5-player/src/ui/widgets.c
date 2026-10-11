/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>

#include "ui/ui.h"

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

void ui_overlay_set(bool show)
{
	static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

	display_sunxi_fb_enable(disp, show);
}

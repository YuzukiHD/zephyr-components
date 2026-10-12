/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The settings row of the brightness of the backlight */

#include "features/settings/settings.h"
#include "ui/ui.h"

static void brightness_changed(lv_event_t *e)
{
	lv_obj_t *s = lv_event_get_target(e);

	/* the backlight follows the slider, the value is saved when it is let go */
	ui_brightness_set(lv_slider_get_value(s), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

static void brightness_row(lv_obj_t *list)
{
	lv_obj_t *value;
	lv_obj_t *row = settings_row_create(list, LV_SYMBOL_EYE_OPEN, "Brightness", &value, NULL);
	lv_obj_t *s = lv_slider_create(row);

	lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_width(s, 360);
	lv_slider_set_range(s, 10, 255);
	lv_slider_set_value(s, ui_brightness(), LV_ANIM_OFF);
	lv_obj_set_style_bg_color(s, lv_color_hex(C_SURFACE2), LV_PART_MAIN);
	lv_obj_set_style_bg_color(s, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(s, lv_color_hex(C_ACCENT), LV_PART_KNOB);
	lv_obj_set_style_pad_all(s, 8, LV_PART_KNOB);
	lv_obj_set_ext_click_area(s, 20);
	lv_obj_add_event_cb(s, brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_add_event_cb(s, brightness_changed, LV_EVENT_RELEASED, NULL);
}

NEKOBOX_SETTINGS_ROW(brightness, 50, brightness_row, NULL);

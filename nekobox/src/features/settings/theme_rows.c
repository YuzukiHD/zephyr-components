/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The settings rows of the theme: dark or light, and the accent colour */

#include "features/settings/settings.h"
#include "ui/ui.h"

static void dark_changed(lv_event_t *e)
{
	lv_obj_t *sw = lv_event_get_target(e);

	/* the screens are made again, the switch with them */
	ui_theme_set(lv_obj_has_state(sw, LV_STATE_CHECKED), ui_theme_accent());
}

static void accent_clicked(lv_event_t *e)
{
	ui_theme_set(ui_theme_dark(), (int)(uintptr_t)lv_event_get_user_data(e));
}

/* a row with a switch for dark or light */
static void dark_row(lv_obj_t *list)
{
	lv_obj_t *value;
	lv_obj_t *row = settings_row_create(list, LV_SYMBOL_EYE_OPEN, "Dark mode", &value, NULL);
	lv_obj_t *sw = lv_switch_create(row);

	lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_style_bg_color(sw, lv_color_hex(C_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
	if (ui_theme_dark()) {
		lv_obj_add_state(sw, LV_STATE_CHECKED);
	}
	lv_obj_add_event_cb(sw, dark_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

/* a row with the accent colours to choose from */
static void accent_row(lv_obj_t *list)
{
	lv_obj_t *value;
	lv_obj_t *row = settings_row_create(list, LV_SYMBOL_IMAGE, "Accent color", &value, NULL);

	lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);
	for (int i = 0; i < UI_ACCENTS; i++) {
		lv_obj_t *dot = lv_button_create(row);

		lv_obj_remove_style_all(dot);
		lv_obj_set_size(dot, 48, 48);
		lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_bg_color(dot, lv_color_hex(ui_theme_accent_color(i)), 0);
		lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
		if (i == ui_theme_accent()) {
			lv_obj_set_style_border_width(dot, 4, 0);
			lv_obj_set_style_border_color(dot, lv_color_hex(C_TEXT), 0);
		}
		lv_obj_set_ext_click_area(dot, 6);
		lv_obj_add_event_cb(dot, accent_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
	}
}

NEKOBOX_SETTINGS_ROW(dark, 60, dark_row, NULL);
NEKOBOX_SETTINGS_ROW(accent, 70, accent_row, NULL);

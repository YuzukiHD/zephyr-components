/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The settings row of the cast target: a switch and a line of status */

#include "cast.h"
#include "core/cfg.h"
#include "features/settings/settings.h"
#include "ui/ui.h"

static lv_obj_t *value_label, *enable_switch;

static void switched(lv_event_t *e)
{
	bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);

	cfg_set("dlna", on ? "1" : "0");
	cast_enable(on);
}

static void create(lv_obj_t *list)
{
	lv_obj_t *row = settings_row_create(list, LV_SYMBOL_VIDEO, "Cast to this player (DLNA)",
					    &value_label, NULL);
	lv_obj_t *sw = lv_switch_create(row);

	enable_switch = sw;
	lv_obj_set_style_bg_color(sw, lv_color_hex(C_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
	if (cast_enabled()) {
		lv_obj_add_state(sw, LV_STATE_CHECKED);
	}
	lv_obj_add_event_cb(sw, switched, LV_EVENT_VALUE_CHANGED, NULL);
}

static void refresh(void)
{
	char line[96];

	cast_status(line, sizeof(line));
	lv_label_set_text(value_label, line);
	if (cast_enabled()) {
		lv_obj_add_state(enable_switch, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(enable_switch, LV_STATE_CHECKED);
	}
}

NEKOBOX_SETTINGS_ROW(cast, 45, create, refresh);

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The settings row that shows the Bluetooth connection */

#include "features/net/bt.h"
#include "features/settings/settings.h"
#include "ui/ui.h"

static lv_obj_t *bt_value;

static void bt_row(lv_obj_t *list)
{
	settings_row_create(list, LV_SYMBOL_BLUETOOTH, "Bluetooth", &bt_value, NULL);
}

static void bt_refresh(void)
{
	char line[96];

	bt_status(line, sizeof(line));
	lv_label_set_text(bt_value, line);
}

NEKOBOX_SETTINGS_ROW(bluetooth, 30, bt_row, bt_refresh);

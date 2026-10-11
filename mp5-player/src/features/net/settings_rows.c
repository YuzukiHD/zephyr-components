/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The settings rows of the network: Wi-Fi, the network time, and Bluetooth when it is built */

#include <stdio.h>

#include "features/net/net.h"
#include "features/settings/settings.h"
#include "ui/ui.h"

static lv_obj_t *net_value, *sync_value;

static void wifi_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto("wifi");
}

static void sync_clicked(lv_event_t *e)
{
	char line[96];

	ARG_UNUSED(e);
	app_net_sync_time();
	app_net_sync_status(line, sizeof(line));
	lv_label_set_text(sync_value, line);
}

static void net_row(lv_obj_t *list)
{
	settings_row_create(list, LV_SYMBOL_WIFI, "Wi-Fi", &net_value, wifi_clicked);
}

static void sync_row(lv_obj_t *list)
{
	settings_row_create(list, LV_SYMBOL_REFRESH, "Set the time from the network", &sync_value,
			    sync_clicked);
}

static void net_refresh(void)
{
	char line[96];

	app_net_status(line, sizeof(line));
	lv_label_set_text(net_value, line);
	app_net_sync_status(line, sizeof(line));
	lv_label_set_text(sync_value, line);
}

MP5_SETTINGS_ROW(wifi, 20, net_row, net_refresh);
MP5_SETTINGS_ROW(sync, 40, sync_row, NULL);

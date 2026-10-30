/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "cast.h"
#include "core/cfg.h"
#include "core/storage.h"
#include "features/net/net.h"
#include "ui/ui.h"

static lv_obj_t *status_label, *help_label, *network_label, *toggle_btn, *toggle_label;
static lv_timer_t *refresh_timer;

static void refresh(lv_timer_t *t)
{
	char status[192], network[128];
	bool on = cast_enabled();
	bool connected = app_net_connected();

	ARG_UNUSED(t);
	cast_status(status, sizeof(status));
	app_net_status(network, sizeof(network));
	lv_label_set_text(status_label, status);
	lv_obj_set_style_text_color(status_label, lv_color_hex(on ? C_ACCENT : C_DIM), 0);
	lv_label_set_text_fmt(network_label, LV_SYMBOL_WIFI "   %s", network);
	lv_label_set_text(toggle_label, on ? LV_SYMBOL_STOP "  Stop receiving" :
					   LV_SYMBOL_PLAY "  Start receiving");
	lv_obj_set_style_bg_color(toggle_btn, lv_color_hex(on ? C_SURFACE2 : C_ACCENT), 0);
	lv_obj_set_style_text_color(toggle_label, on ? lv_color_hex(C_TEXT) : lv_color_white(), 0);
	if (!storage_ready()) {
		lv_label_set_text(help_label, "Insert an SD card to receive videos.");
	} else if (!on) {
		lv_label_set_text(help_label, "Start receiving to let your phone or PC find this player.");
	} else if (!connected) {
		lv_label_set_text(help_label, "Connect this player and your phone or PC to the same Wi-Fi.");
	} else {
		lv_label_set_text_fmt(help_label, "Select %up (%ux%u) or lower in your video app.\n"
						"Open Cast and choose this device.",
				     MIN(CONFIG_NEKOBOX_DLNA_MAX_WIDTH, CONFIG_NEKOBOX_DLNA_MAX_HEIGHT),
				     CONFIG_NEKOBOX_DLNA_MAX_WIDTH, CONFIG_NEKOBOX_DLNA_MAX_HEIGHT);
	}
}

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto("home");
}

static void toggle_clicked(lv_event_t *e)
{
	bool on = !cast_enabled();

	ARG_UNUSED(e);
	cfg_set("dlna", on ? "1" : "0");
	cast_enable(on);
	refresh(NULL);
}

static void wifi_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto_arg("wifi", "dlna");
}

static lv_obj_t *label_create(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
			      int32_t x, int32_t y, int32_t width, const char *text)
{
	lv_obj_t *label = lv_label_create(parent);

	lv_obj_set_style_text_font(label, font, 0);
	lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
	lv_obj_set_width(label, width);
	lv_obj_set_pos(label, x, y);
	lv_label_set_text(label, text);
	return label;
}

static lv_obj_t *button_create(lv_obj_t *parent, int32_t x, int32_t width, const char *text,
			       lv_event_cb_t cb, lv_obj_t **label)
{
	lv_obj_t *button = lv_button_create(parent);
	lv_obj_t *title = lv_label_create(button);

	lv_obj_remove_style_all(button);
	lv_obj_set_size(button, width, 76);
	lv_obj_set_pos(button, x, 420);
	lv_obj_set_style_radius(button, 22, 0);
	lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(button, lv_color_hex(C_SURFACE2), 0);
	lv_obj_set_style_bg_color(button, lv_color_hex(C_ACCENT2), LV_STATE_PRESSED);
	lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(title, lv_color_hex(C_TEXT), 0);
	lv_label_set_text(title, text);
	lv_obj_center(title);
	if (label != NULL) {
		*label = title;
	}
	return button;
}

static lv_obj_t *cast_create(void)
{
	lv_obj_t *scr = ui_screen_new(false);
	lv_obj_t *card, *name;

	ui_title_bar(scr, "DLNA", back_clicked);
	card = lv_obj_create(scr);
	lv_obj_remove_style_all(card);
	lv_obj_set_pos(card, 40, 112);
	lv_obj_set_size(card, 944, 536);
	lv_obj_set_style_radius(card, 32, 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_border_width(card, 1, 0);
	lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

	label_create(card, &lv_font_montserrat_28, C_ACCENT, 32, 32, 880,
		     LV_SYMBOL_VIDEO "  Cast to this player");
	label_create(card, &lv_font_montserrat_20, C_DIM, 32, 96, 880, "Device name");
	name = label_create(card, &lv_font_montserrat_48, C_TEXT, 32, 132, 880,
			    CONFIG_NEKOBOX_DLNA_NAME);
	lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
	status_label = label_create(card, &lv_font_montserrat_20, C_DIM, 32, 212, 880, "");
	help_label = label_create(card, &lv_font_montserrat_24, C_TEXT, 32, 284, 880, "");
	lv_obj_set_style_text_line_space(help_label, 8, 0);
	toggle_btn = button_create(card, 32, 416, "", toggle_clicked, &toggle_label);
	button_create(card, 472, 440, LV_SYMBOL_WIFI "  Wi-Fi settings", wifi_clicked, NULL);

	network_label = label_create(scr, &lv_font_montserrat_20, C_DIM, 48, 684, 928, "");
	lv_label_set_long_mode(network_label, LV_LABEL_LONG_DOT);
	return scr;
}

static void cast_enter(void *arg)
{
	ARG_UNUSED(arg);
	if (refresh_timer == NULL) {
		refresh_timer = lv_timer_create(refresh, 500, NULL);
	}
	lv_timer_resume(refresh_timer);
	refresh(NULL);
}

static void cast_leave(void)
{
	if (refresh_timer != NULL) {
		lv_timer_pause(refresh_timer);
	}
}

NEKOBOX_SCREEN(dlna, "dlna", cast_create, cast_enter, cast_leave);
NEKOBOX_HOME_TILE(dlna, .order = 45, .symbol = LV_SYMBOL_VIDEO, .title = "DLNA",
		  .subtitle = "Cast from a phone or PC", .color_top = 0x27C5AF,
		  .color_bottom = 0x158EAB, .screen = "dlna");

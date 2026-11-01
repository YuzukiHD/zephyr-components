/* SPDX-License-Identifier: Apache-2.0 */

#include <miracast/player.h>
#include <zephyr/kernel.h>

#include "ui/ui.h"
#include "session.h"

static lv_obj_t *card, *status_label, *toggle_btn, *toggle_label;
static lv_timer_t *timer;
static bool visible;
static enum mirror_state shown = -1;

static void refresh(lv_timer_t *t)
{
	ARG_UNUSED(t);
	mirror_tick();
	enum mirror_state state = mirror_state();

	if (!visible) {
		if (state == MIRROR_OFF || state == MIRROR_FAILED) lv_timer_pause(timer);
		return;
	}
	if (state == MIRROR_PLAYING && miracast_player_frames()) {
		lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);
	}
	if (state == shown) return;
	shown = state;
	static const char *const messages[] = {
		[MIRROR_OFF] = "Ready. Start receiving to mirror your phone.",
		[MIRROR_PREPARING] = "Pausing other casting services...",
		[MIRROR_STARTING] = "Preparing the receiver...",
		[MIRROR_WAITING] = "Waiting for your Android phone",
		[MIRROR_CONNECTING] = "Connecting to your phone...",
		[MIRROR_PLAYING] = "Connected, waiting for the first picture...",
		[MIRROR_STOPPING] = "Stopping screen mirroring...",
		[MIRROR_FAILED] = "Screen mirroring could not start. Try again.",
	};

	lv_label_set_text(status_label, messages[state]);
	bool off = state == MIRROR_OFF || state == MIRROR_FAILED;

	lv_label_set_text(toggle_label, off ? LV_SYMBOL_PLAY "  Start receiving" :
		state == MIRROR_STOPPING ? "Stopping..." : LV_SYMBOL_STOP "  Stop receiving");
	lv_obj_set_style_bg_color(toggle_btn, lv_color_hex(off ? C_ACCENT : C_SURFACE2), 0);
	lv_obj_set_style_text_color(toggle_label, off ? lv_color_white() : lv_color_hex(C_TEXT), 0);
	if (state == MIRROR_STOPPING) {
		lv_obj_add_state(toggle_btn, LV_STATE_DISABLED);
	} else {
		lv_obj_remove_state(toggle_btn, LV_STATE_DISABLED);
	}
}

static void close_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto("home");
}

static void toggle_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	enum mirror_state state = mirror_state();

	if (state == MIRROR_OFF || state == MIRROR_FAILED) {
		mirror_start();
	} else {
		mirror_stop();
	}
	refresh(NULL);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
		       int32_t y, const char *text)
{
	lv_obj_t *obj = lv_label_create(parent);

	lv_obj_set_style_text_font(obj, font, 0);
	lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(obj, 752);
	lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
	lv_label_set_text(obj, text);
	return obj;
}

static lv_obj_t *mirror_create(void)
{
	lv_obj_t *scr = ui_screen_new(true);

	card = lv_obj_create(scr);
	lv_obj_remove_style_all(card);
	lv_obj_set_size(card, 832, 472);
	lv_obj_center(card);
	lv_obj_set_style_radius(card, 32, 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(card, 1, 0);
	lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
	label(card, &lv_font_montserrat_28, C_ACCENT, 32, LV_SYMBOL_WIFI "  Miracast");
	lv_obj_t *name = label(card, &lv_font_montserrat_48, C_TEXT, 88,
			       CONFIG_NEKOBOX_MIRACAST_NAME);

	lv_obj_set_height(name, 60);
	lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
	status_label = label(card, &lv_font_montserrat_20, C_ACCENT, 164, "");
	lv_obj_t *help = label(card, &lv_font_montserrat_24, C_TEXT, 212,
		"Open Wireless display / Screen mirroring\non your Android phone and select this display.");

	lv_obj_set_style_text_line_space(help, 8, 0);
	label(card, &lv_font_montserrat_16, C_DIM, 296,
		"Your usual Wi-Fi connection resumes after stopping. No SD card needed.");
	toggle_btn = lv_button_create(card);
	lv_obj_remove_style_all(toggle_btn);
	lv_obj_set_size(toggle_btn, 440, 72);
	lv_obj_align(toggle_btn, LV_ALIGN_TOP_MID, 0, 352);
	lv_obj_set_style_radius(toggle_btn, 22, 0);
	lv_obj_set_style_bg_opa(toggle_btn, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(toggle_btn, lv_color_hex(C_ACCENT2), LV_STATE_PRESSED);
	lv_obj_add_event_cb(toggle_btn, toggle_clicked, LV_EVENT_CLICKED, NULL);
	toggle_label = lv_label_create(toggle_btn);
	lv_obj_set_style_text_font(toggle_label, &lv_font_montserrat_24, 0);
	lv_obj_center(toggle_label);
	lv_obj_t *close = ui_icon_button(scr, LV_SYMBOL_CLOSE, 88);

	lv_obj_set_style_bg_opa(close, LV_OPA_70, 0);
	lv_obj_set_ext_click_area(close, 32);
	lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -24, 24);
	lv_obj_add_event_cb(close, close_clicked, LV_EVENT_CLICKED, NULL);
	return scr;
}

static void mirror_enter(void *arg)
{
	ARG_UNUSED(arg);
	visible = true;
	shown = -1;
	if (!timer) timer = lv_timer_create(refresh, 100, NULL);
	lv_timer_resume(timer);
	mirror_start();
	refresh(NULL);
}

static void mirror_leave(void)
{
	visible = false;
	mirror_stop();
	ui_overlay_set(true);
}

NEKOBOX_SCREEN(miracast, "miracast", mirror_create, mirror_enter, mirror_leave);
NEKOBOX_HOME_TILE(miracast, .order = 46, .symbol = LV_SYMBOL_WIFI, .title = "Miracast",
	.subtitle = "Mirror an Android screen", .color_top = 0x42B8FF, .color_bottom = 0x5266DC,
	.screen = "miracast");

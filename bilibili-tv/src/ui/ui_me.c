/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The "mine" tab of the home page: while nobody is logged in a QR code to scan with the Bilibili
 * app, afterwards the user name and a logout button.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "ui/ui_priv.h"

#define QR_SIZE		300
#define POLL_MS		2000

static lv_obj_t *area, *avatar, *avatar_img, *avatar_letter, *name_label, *sub_label, *hint_label, *action_btn, *action_label;
static lv_obj_t *qr_holder, *qr_obj, *state_label;
static uint32_t qr_gen, avatar_seen;
static lv_image_dsc_t avatar_dsc;
static int64_t last_poll;
static enum qr_state shown_state;
static bool shown_logged, shown_valid;
static char shown_name[64];

static void action_clicked(lv_event_t *e)
{
	if (app_account(NULL, 0)) {
		app_cmd(CMD_LOGOUT, 0);
	} else {
		app_cmd(CMD_QR_START, 0);
	}
}

/* the first character of a UTF-8 text, for the round picture of the user */
static void first_char(const char *s, char *out, size_t size)
{
	size_t n = 1;

	while (((uint8_t)s[n] & 0xc0) == 0x80) {
		n++;
	}
	if (s[0] == '\0' || n >= size) {
		out[0] = '\0';
		return;
	}
	memcpy(out, s, n);
	out[n] = '\0';
}

lv_obj_t *ui_me_create(lv_obj_t *parent, int top, int height)
{
	lv_obj_t *left, *right;

	area = lv_obj_create(parent);
	lv_obj_set_pos(area, 0, top);
	lv_obj_set_size(area, SCREEN_W, height);
	lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(area, 0, 0);
	lv_obj_set_style_pad_all(area, 0, 0);
	lv_obj_remove_flag(area, LV_OBJ_FLAG_SCROLLABLE);

	/* the user */
	left = ui_card_panel(area, 24, 12, 470, height - 24);
	avatar = lv_obj_create(left);
	lv_obj_set_size(avatar, 96, 96);
	lv_obj_set_pos(avatar, 32, 32);
	lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_border_width(avatar, 0, 0);
	lv_obj_set_style_pad_all(avatar, 0, 0);
	lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
	lv_obj_remove_flag(avatar, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_clip_corner(avatar, true, 0);
	avatar_letter = ui_text(avatar, ui_font_l, lv_color_white(), 0, 0);
	lv_obj_center(avatar_letter);
	avatar_img = lv_image_create(avatar);
	lv_obj_set_pos(avatar_img, 0, 0);
	lv_obj_add_flag(avatar_img, LV_OBJ_FLAG_HIDDEN);
	name_label = ui_text(left, ui_font_l, C_TEXT, 152, 44);
	lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(name_label, 290);
	sub_label = ui_text(left, ui_font_s, C_DIM, 152, 88);
	lv_obj_set_width(sub_label, 290);
	hint_label = ui_text(left, ui_font_m, C_DIM, 32, 170);
	lv_label_set_long_mode(hint_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(hint_label, 406);
	lv_obj_set_style_text_line_space(hint_label, 8, 0);
	action_btn = ui_button(left, "", action_clicked, NULL);
	action_label = lv_obj_get_child(action_btn, 0);
	ui_button_size(action_btn, 406, 56);
	lv_obj_set_pos(action_btn, 32, height - 24 - 56 - 32);

	/* the code, dark modules on a white ground whatever the theme is */
	right = ui_card_panel(area, 518, 12, 482, height - 24);
	qr_holder = lv_obj_create(right);
	lv_obj_set_size(qr_holder, QR_SIZE + 40, QR_SIZE + 40);
	lv_obj_set_pos(qr_holder, (482 - QR_SIZE - 40) / 2, 40);
	lv_obj_set_style_bg_color(qr_holder, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(qr_holder, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(qr_holder, 20, 0);
	lv_obj_set_style_border_width(qr_holder, 0, 0);
	lv_obj_set_style_pad_all(qr_holder, 0, 0);
	lv_obj_remove_flag(qr_holder, LV_OBJ_FLAG_SCROLLABLE);
	qr_obj = lv_qrcode_create(qr_holder);
	lv_qrcode_set_size(qr_obj, QR_SIZE);
	lv_qrcode_set_dark_color(qr_obj, lv_color_black());
	lv_qrcode_set_light_color(qr_obj, lv_color_white());
	lv_obj_center(qr_obj);
	state_label = ui_text(right, ui_font_m, C_TEXT, 20, QR_SIZE + 40 + 64);
	lv_label_set_long_mode(state_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(state_label, 442);
	lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);

	return area;
}

void ui_me_show(void)
{
	shown_valid = false;
	if (!app_account(NULL, 0)) {
		app_cmd(CMD_QR_START, 0);
	}
	last_poll = k_uptime_get();
}

static const char *state_text(enum qr_state st)
{
	switch (st) {
	case QR_SCANNED:
		return "\xe5\xb7\xb2\xe6\x89\xab\xe7\xa0\x81\xef\xbc\x8c\xe8\xaf\xb7\xe5\x9c\xa8\xe6\x89\x8b\xe6\x9c\xba\xe4\xb8\x8a\xe7\xa1\xae\xe8\xae\xa4\xe7\x99\xbb\xe5\xbd\x95";	/* scanned, confirm on the phone */
	case QR_EXPIRED:
		return "\xe4\xba\x8c\xe7\xbb\xb4\xe7\xa0\x81\xe5\xb7\xb2\xe8\xbf\x87\xe6\x9c\x9f\xef\xbc\x8c\xe8\xaf\xb7\xe5\x88\xb7\xe6\x96\xb0";	/* expired */
	case QR_ERROR:
		return "\xe7\x99\xbb\xe5\xbd\x95\xe5\x87\xba\xe9\x94\x99\xef\xbc\x8c\xe8\xaf\xb7\xe5\x88\xb7\xe6\x96\xb0\xe4\xba\x8c\xe7\xbb\xb4\xe7\xa0\x81\xe9\x87\x8d\xe8\xaf\x95";	/* login failed, refresh the code */
	case QR_NONE:
		return "\xe6\xad\xa3\xe5\x9c\xa8\xe8\x8e\xb7\xe5\x8f\x96\xe4\xba\x8c\xe7\xbb\xb4\xe7\xa0\x81...";	/* getting the code */
	default:
		return "\xe7\x94\xa8 bilibili \xe6\x89\x8b\xe6\x9c\xba\xe5\xae\xa2\xe6\x88\xb7\xe7\xab\xaf\xe6\x89\xab\xe7\xa0\x81\xe7\x99\xbb\xe5\xbd\x95";	/* scan with the app */
	}
}

void ui_me_run(bool visible)
{
	struct qr_info qi;
	char name[64], letter[8];
	bool logged;

	if (!visible) {
		return;
	}
	logged = app_account(name, sizeof(name));
	app_qr_info(&qi);

	{
		const uint16_t *px;
		uint32_t gen;
		bool have = app_avatar(&px, &gen);

		if (gen != avatar_seen || !shown_valid) {
			avatar_seen = gen;
			if (have && logged) {
				avatar_dsc = (lv_image_dsc_t){
					.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
						   .w = AVATAR_SIZE, .h = AVATAR_SIZE, .stride = AVATAR_STRIDE},
					.data_size = AVATAR_STRIDE * AVATAR_SIZE,
					.data = (const uint8_t *)px,
				};
				lv_image_cache_drop(&avatar_dsc);
				lv_image_set_src(avatar_img, &avatar_dsc);
				lv_obj_invalidate(avatar_img);
				lv_obj_remove_flag(avatar_img, LV_OBJ_FLAG_HIDDEN);
				lv_obj_add_flag(avatar_letter, LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_obj_add_flag(avatar_img, LV_OBJ_FLAG_HIDDEN);
				lv_obj_remove_flag(avatar_letter, LV_OBJ_FLAG_HIDDEN);
			}
		}
	}
	if (logged) {
		if (!shown_valid || !shown_logged || strcmp(shown_name, name) != 0) {
			const char *n = name[0] != '\0' ? name : "bilibili";

			first_char(n, letter, sizeof(letter));
			lv_label_set_text(avatar_letter, letter);
			lv_obj_set_style_bg_color(avatar, C_ACCENT, 0);
			lv_label_set_text(name_label, n);
			lv_label_set_text(sub_label, "\xe5\xb7\xb2\xe7\x99\xbb\xe5\xbd\x95");				/* logged in */
			lv_label_set_text(hint_label, "\xe6\x8e\xa8\xe8\x8d\x90\xe5\xb7\xb2\xe6\xa0\xb9\xe6\x8d\xae\xe4\xbd\xa0\xe6\x9b\xb4\xe6\x96\xb0\xef\xbc\x8c"
					  "\xe5\x85\xb3\xe6\xb3\xa8\xe9\xa1\xb5\xe7\x9a\x84\xe8\xa7\x86\xe9\xa2\x91\xe5\x9c\xa8\xe4\xb8\xbb\xe9\xa1\xb5\xe7\x9a\x84"
					  "\xe3\x80\x8c\xe5\x85\xb3\xe6\xb3\xa8\xe3\x80\x8d\xe9\x87\x8c\xe3\x80\x82");
			lv_label_set_text(action_label, "\xe9\x80\x80\xe5\x87\xba\xe7\x99\xbb\xe5\xbd\x95");			/* log out */
			lv_label_set_text(state_label, "");
			lv_obj_add_flag(qr_holder, LV_OBJ_FLAG_HIDDEN);
			snprintf(shown_name, sizeof(shown_name), "%s", name);
			shown_logged = true;
			shown_valid = true;
		}
		return;
	}

	if (!shown_valid || shown_logged || shown_state != qi.state) {
		lv_label_set_text(avatar_letter, "?");
		lv_obj_set_style_bg_color(avatar, C_CARD_PRESS, 0);
		lv_label_set_text(name_label, "\xe6\x9c\xaa\xe7\x99\xbb\xe5\xbd\x95");					/* not logged in */
		lv_label_set_text(sub_label, "\xe6\x89\xab\xe7\xa0\x81\xe7\x99\xbb\xe5\xbd\x95");			/* log in by scanning */
		lv_label_set_text(hint_label, "\xe7\x99\xbb\xe5\xbd\x95\xe5\x90\x8e\xe4\xb8\xbb\xe9\xa1\xb5\xe6\x8e\xa8\xe8\x8d\x90\xe4\xbc\x9a\xe6\x9b\xb4\xe6\x87\x82\xe4\xbd\xa0\xef\xbc\x8c"
				  "\xe5\xb9\xb6\xe5\x8f\xaf\xe4\xbb\xa5\xe6\x9f\xa5\xe7\x9c\x8b\xe5\x85\xb3\xe6\xb3\xa8\xe7\x9a\x84\xe4\xba\xba\xe7\x9a\x84"
				  "\xe8\xa7\x86\xe9\xa2\x91\xe3\x80\x82");
		lv_label_set_text(action_label, "\xe5\x88\xb7\xe6\x96\xb0\xe4\xba\x8c\xe7\xbb\xb4\xe7\xa0\x81");		/* refresh the code */
		lv_label_set_text(state_label, state_text(qi.state));
		lv_obj_set_style_text_color(state_label, qi.state == QR_SCANNED ? C_ACCENT : C_TEXT, 0);
		shown_state = qi.state;
		shown_logged = false;
		shown_valid = true;
		if (qi.state == QR_WAIT || qi.state == QR_SCANNED) {
			lv_obj_remove_flag(qr_holder, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(qr_holder, LV_OBJ_FLAG_HIDDEN);
		}
	}
	if (qi.gen != qr_gen && qi.url[0] != '\0') {
		qr_gen = qi.gen;
		lv_qrcode_update(qr_obj, qi.url, strlen(qi.url));
	}
	if ((qi.state == QR_WAIT || qi.state == QR_SCANNED) && k_uptime_get() - last_poll >= POLL_MS) {
		last_poll = k_uptime_get();
		app_cmd(CMD_QR_POLL, 0);
	}
}

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A pocket calculator: digits, + - * /, percent, sign, backspace. An operator applies the one
 * before it at once (1 + 2 * 3 gives 9), like the calculators it imitates.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "ui/registry.h"
#include "ui/ui.h"

#define DISP_MAX	14	/* digits that can be typed */

static const char *const keys[] = {
	"C", "+/-", "%", "/", "\n",
	"7", "8", "9", "*", "\n",
	"4", "5", "6", "-", "\n",
	"1", "2", "3", "+", "\n",
	"0", ".", "DEL", "=", "",
};

static lv_obj_t *value_label, *expr_label;
static char disp[32] = "0";
static double acc;
static char op;		/* the operator waiting for its second operand, 0: none */
static bool fresh = true;	/* the next digit starts a new number */
static bool error;

/* the minimal C library has no strtod and no %g */
static double parse(const char *s)
{
	bool neg = (*s == '-');
	double v = 0, scale = 0.1;

	if (neg) {
		s++;
	}
	for (; *s >= '0' && *s <= '9'; s++) {
		v = v * 10 + (*s - '0');
	}
	if (*s == '.') {
		for (s++; *s >= '0' && *s <= '9'; s++, scale /= 10) {
			v += (*s - '0') * scale * 10;
		}
	}

	return neg ? -v : v;
}

/* up to 9 decimals, trailing zeros dropped; false when the number is too big to show */
static bool format(char *out, size_t size, double v)
{
	long long whole, frac;
	bool neg = v < 0;
	int n;

	if (neg) {
		v = -v;
	}
	if (!(v < 1e15)) {
		return false;
	}
	whole = (long long)v;
	frac = (long long)((v - (double)whole) * 1e9 + 0.5);
	if (frac >= 1000000000LL) {
		whole++;
		frac -= 1000000000LL;
	}
	n = snprintf(out, size, "%s%lld", neg && (whole != 0 || frac != 0) ? "-" : "", whole);
	if (frac != 0) {
		int digits = 9;

		while (frac % 10 == 0) {
			frac /= 10;
			digits--;
		}
		snprintf(out + n, size - n, ".%0*lld", digits, frac);
	}

	return true;
}

static void show(void)
{
	char expr[48] = "";

	if (op != 0) {
		char n[24];

		if (format(n, sizeof(n), acc)) {
			snprintf(expr, sizeof(expr), "%s %c", n, op);
		}
	}
	lv_label_set_text(value_label, disp);
	lv_label_set_text(expr_label, expr);
}

static void set_value(double v)
{
	if (v != v || !format(disp, sizeof(disp), v)) {
		strcpy(disp, "Error");
		error = true;
	}
	fresh = true;
}

static void apply(void)
{
	double b = parse(disp);

	switch (op) {
	case '+':
		acc += b;
		break;
	case '-':
		acc -= b;
		break;
	case '*':
		acc *= b;
		break;
	case '/':
		if (b == 0) {
			strcpy(disp, "Error");
			error = true;
			op = 0;
			return;
		}
		acc /= b;
		break;
	default:
		acc = b;
		break;
	}
	set_value(acc);
}

static void reset(void)
{
	strcpy(disp, "0");
	acc = 0;
	op = 0;
	fresh = true;
	error = false;
}

static void press(const char *k)
{
	size_t len = strlen(disp);

	if (strcmp(k, "C") == 0) {
		reset();
		return;
	}
	if (error) {
		return;
	}

	if (k[0] >= '0' && k[0] <= '9' && k[1] == '\0') {
		if (fresh || strcmp(disp, "0") == 0) {
			disp[0] = '\0';
			len = 0;
		}
		if (len < DISP_MAX) {
			disp[len] = k[0];
			disp[len + 1] = '\0';
		}
		fresh = false;
	} else if (strcmp(k, ".") == 0) {
		if (fresh) {
			strcpy(disp, "0");
			len = 1;
		}
		if (strchr(disp, '.') == NULL && len < DISP_MAX) {
			strcat(disp, ".");
		}
		fresh = false;
	} else if (strcmp(k, "DEL") == 0) {
		if (!fresh) {
			disp[len - 1] = '\0';
			if (disp[0] == '\0' || strcmp(disp, "-") == 0) {
				strcpy(disp, "0");
			}
		}
	} else if (strcmp(k, "+/-") == 0) {
		if (strcmp(disp, "0") != 0) {
			if (disp[0] == '-') {
				memmove(disp, disp + 1, len);
			} else if (len < sizeof(disp) - 1) {
				memmove(disp + 1, disp, len + 1);
				disp[0] = '-';
			}
		}
	} else if (strcmp(k, "%") == 0) {
		set_value(parse(disp) / 100.0);
	} else if (strcmp(k, "=") == 0) {
		if (op != 0) {
			apply();
			op = 0;
		}
	} else {
		/* + - * / */
		if (op != 0 && !fresh) {
			apply();
		} else if (op == 0) {
			acc = parse(disp);
		}
		if (!error) {
			op = k[0];
			fresh = true;
		}
	}
	show();
}

static void keys_changed(lv_event_t *e)
{
	lv_obj_t *bm = lv_event_get_target_obj(e);
	uint32_t id = lv_buttonmatrix_get_selected_button(bm);

	if (id != LV_BUTTONMATRIX_BUTTON_NONE) {
		press(lv_buttonmatrix_get_button_text(bm, id));
	}
}

/* the keys are coloured by their kind: digits, functions, operators and the equals key */
static void keys_draw(lv_event_t *e)
{
	lv_obj_t *bm = lv_event_get_target_obj(e);
	lv_draw_task_t *task = lv_event_get_draw_task(e);
	lv_draw_dsc_base_t *base = lv_draw_task_get_draw_dsc(task);
	lv_draw_fill_dsc_t *fill;
	lv_draw_label_dsc_t *label;
	const char *k;
	uint32_t bg, fg = C_TEXT;
	bool down;

	if (base->part != LV_PART_ITEMS) {
		return;
	}
	k = lv_buttonmatrix_get_button_text(bm, base->id1);
	down = (lv_obj_get_state(bm) & LV_STATE_PRESSED) &&
	       lv_buttonmatrix_get_selected_button(bm) == base->id1;

	if (strcmp(k, "=") == 0) {
		bg = C_ACCENT;
		fg = 0xFFFFFF;
	} else if (strchr("+-*/", k[0]) != NULL && k[1] == '\0') {
		bg = C_ACCENT2;
		fg = 0xFFFFFF;
	} else if (strcmp(k, "C") == 0 || strcmp(k, "+/-") == 0 || strcmp(k, "%") == 0 ||
		   strcmp(k, "DEL") == 0) {
		bg = C_SURFACE2;
	} else {
		bg = C_SURFACE;
	}

	fill = lv_draw_task_get_fill_dsc(task);
	if (fill != NULL) {
		fill->color = down ? lv_color_lighten(lv_color_hex(bg), 40) : lv_color_hex(bg);
	}
	label = lv_draw_task_get_label_dsc(task);
	if (label != NULL) {
		label->color = lv_color_hex(fg);
	}
}

static void back_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	ui_goto("home");
}

static lv_obj_t *calc_create(void)
{
	lv_obj_t *scr = ui_screen_new(false);
	lv_obj_t *card, *bm;

	ui_title_bar(scr, "Calculator", back_clicked);

	card = lv_obj_create(scr);
	lv_obj_remove_style_all(card);
	lv_obj_set_size(card, 960, 140);
	lv_obj_set_pos(card, 32, 104);
	lv_obj_set_style_radius(card, 24, 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(C_SURFACE), 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_hor(card, 28, 0);
	lv_obj_set_style_pad_ver(card, 12, 0);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

	expr_label = lv_label_create(card);
	lv_obj_set_width(expr_label, LV_PCT(100));
	lv_obj_set_style_text_font(expr_label, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(expr_label, lv_color_hex(C_DIM), 0);
	lv_obj_set_style_text_align(expr_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_align(expr_label, LV_ALIGN_TOP_RIGHT, 0, 0);

	value_label = lv_label_create(card);
	lv_obj_set_width(value_label, LV_PCT(100));
	lv_label_set_long_mode(value_label, LV_LABEL_LONG_CLIP);
	lv_obj_set_style_text_font(value_label, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(value_label, lv_color_hex(C_TEXT), 0);
	lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_align(value_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

	bm = lv_buttonmatrix_create(scr);
	lv_buttonmatrix_set_map(bm, keys);
	lv_obj_remove_style_all(bm);
	lv_obj_set_size(bm, 960, 768 - 264 - 24);
	lv_obj_set_pos(bm, 32, 264);
	lv_obj_set_style_pad_row(bm, 12, 0);
	lv_obj_set_style_pad_column(bm, 12, 0);
	lv_obj_set_style_radius(bm, 22, LV_PART_ITEMS);
	lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS);
	lv_obj_set_style_border_width(bm, 0, LV_PART_ITEMS);
	lv_obj_set_style_shadow_width(bm, 0, LV_PART_ITEMS);
	lv_obj_set_style_text_font(bm, &lv_font_montserrat_48, LV_PART_ITEMS);
	lv_obj_add_flag(bm, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
	lv_obj_add_event_cb(bm, keys_draw, LV_EVENT_DRAW_TASK_ADDED, NULL);
	lv_obj_add_event_cb(bm, keys_changed, LV_EVENT_VALUE_CHANGED, NULL);

	show();

	return scr;
}

MP5_SCREEN(calculator, "calculator", calc_create, NULL, NULL);
MP5_HOME_TILE(calculator, .order = 65, .symbol = LV_SYMBOL_LIST, .title = "Calculator",
	      .subtitle = "Four functions", .color_top = 0xF59E0B, .color_bottom = 0xD9480F,
	      .screen = "calculator");

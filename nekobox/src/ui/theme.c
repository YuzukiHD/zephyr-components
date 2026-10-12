/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The colours: dark or light, one of the accents, saved in the settings; and the brightness */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

#include "core/app.h"
#include "core/cfg.h"
#include "ui/ui.h"

static const struct {
	const char *name;
	uint32_t accent, accent2;
} accents[UI_ACCENTS] = {
	{"Blue", 0x6C8CFF, 0x9B6CFF},
	{"Purple", 0x9B6CFF, 0xD443A8},
	{"Green", 0x3DDC97, 0x1FA97A},
	{"Orange", 0xFFB547, 0xF2822C},
	{"Pink", 0xFF6C9B, 0xD443A8},
	{"Red", 0xE5484D, 0xB02A37},
};

struct palette ui_pal = {
	.bg = 0x0B0F17, .surface = 0x151B28, .surface2 = 0x1F2740, .border = 0x2A3350,
	.text = 0xE8ECF4, .dim = 0x8B95A8, .accent = 0x6C8CFF, .accent2 = 0x9B6CFF,
	.ok = 0x3DDC97, .warn = 0xFFB547, .pink = 0xFF6C9B,
};
static bool theme_dark = true;
static int theme_accent;

bool ui_theme_dark(void)
{
	return theme_dark;
}

int ui_theme_accent(void)
{
	return theme_accent;
}

const char *ui_theme_accent_name(int index)
{
	return accents[CLAMP(index, 0, UI_ACCENTS - 1)].name;
}

uint32_t ui_theme_accent_color(int index)
{
	return accents[CLAMP(index, 0, UI_ACCENTS - 1)].accent;
}

static void palette_make(bool dark, int accent)
{
	theme_dark = dark;
	theme_accent = CLAMP(accent, 0, UI_ACCENTS - 1);
	if (dark) {
		ui_pal = (struct palette){
			.bg = 0x0B0F17, .surface = 0x151B28, .surface2 = 0x1F2740, .border = 0x2A3350,
			.text = 0xE8ECF4, .dim = 0x8B95A8,
			.ok = 0x3DDC97, .warn = 0xFFB547, .pink = 0xFF6C9B,
		};
	} else {
		ui_pal = (struct palette){
			.bg = 0xF1F3F9, .surface = 0xFFFFFF, .surface2 = 0xDFE5F2, .border = 0xCBD3E4,
			.text = 0x1B2233, .dim = 0x5C687F,
			.ok = 0x1FA97A, .warn = 0xD9830A, .pink = 0xE0457B,
		};
	}
	ui_pal.accent = accents[theme_accent].accent;
	ui_pal.accent2 = accents[theme_accent].accent2;
}

/* the saved theme, before the screens are built */
static void theme_load(void)
{
	const char *mode = cfg_get("theme");
	const char *acc = cfg_get("accent");

	palette_make(mode == NULL || strcmp(mode, "light") != 0, acc != NULL ? atoi(acc) : 0);
}

#define BRIGHTNESS_DEFAULT 10	/* the lowest the slider allows */

static int brightness = BRIGHTNESS_DEFAULT;

int ui_brightness(void)
{
	return brightness;
}

void ui_brightness_set(int value, bool save)
{
	static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	char buf[8];

	brightness = CLAMP(value, 10, 255);
	display_set_brightness(disp, brightness);
	if (save) {
		snprintf(buf, sizeof(buf), "%d", brightness);
		cfg_set("brightness", buf);
	}
}

void ui_theme_set(bool dark, int accent)
{
	char buf[8];

	palette_make(dark, accent);
	cfg_set("theme", dark ? "dark" : "light");
	snprintf(buf, sizeof(buf), "%d", theme_accent);
	cfg_set("accent", buf);
	/* the screen that asks is one of those to be deleted: do it after its event */
	ui_rebuild_all();
}

void ui_theme_reload(void)
{
	bool dark = ui_theme_dark();
	int accent = theme_accent;
	const char *b = cfg_get("brightness");

	theme_load();
	if (b != NULL) {
		ui_brightness_set(atoi(b), false);
	}
	if (dark != ui_theme_dark() || accent != theme_accent) {
		ui_rebuild_all();
	}
}

static int theme_step(void)
{
	const char *b;

	theme_load();
	b = cfg_get("brightness");
	ui_brightness_set(b != NULL ? atoi(b) : BRIGHTNESS_DEFAULT, false);

	return 0;
}
NEKOBOX_INIT(NEKOBOX_STAGE_UI, 15, "theme", theme_step);

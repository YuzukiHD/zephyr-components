/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The colours of the user interface: they change with the theme (dark or light, and the accent) */

#ifndef NEKOBOX_UI_THEME_H_
#define NEKOBOX_UI_THEME_H_

#include <stdbool.h>
#include <stdint.h>

struct palette {
	uint32_t bg, surface, surface2, border, text, dim, accent, accent2, ok, warn, pink;
};

extern struct palette ui_pal;

#define C_BG		(ui_pal.bg)
#define C_SURFACE	(ui_pal.surface)
#define C_SURFACE2	(ui_pal.surface2)
#define C_BORDER	(ui_pal.border)
#define C_TEXT		(ui_pal.text)
#define C_DIM		(ui_pal.dim)
#define C_ACCENT	(ui_pal.accent)
#define C_ACCENT2	(ui_pal.accent2)
#define C_OK		(ui_pal.ok)
#define C_WARN		(ui_pal.warn)
#define C_PINK		(ui_pal.pink)

#define UI_ACCENTS	6

bool ui_theme_dark(void);
int ui_theme_accent(void);
const char *ui_theme_accent_name(int index);
uint32_t ui_theme_accent_color(int index);

/** Sets the theme, saves it and builds the screens again */
void ui_theme_set(bool dark, int accent);

/** Takes the saved theme and brightness again, after the settings file became readable */
void ui_theme_reload(void);

/** Brightness of the backlight, 10..255 */
int ui_brightness(void);
/** Sets the brightness; @p save also writes it to the settings */
void ui_brightness_set(int value, bool save);

#endif /* NEKOBOX_UI_THEME_H_ */

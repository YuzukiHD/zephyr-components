/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * What the features register with the user interface. The records are read only data in the
 * flash, found by the linker (src/ui/sections-rom.ld), so a feature that is not built is simply
 * not in the lists: no tile on the home screen, no row in the settings, no file type.
 */

#ifndef NEKOBOX_UI_REGISTRY_H_
#define NEKOBOX_UI_REGISTRY_H_

#include <lvgl.h>
#include <zephyr/sys/iterable_sections.h>

/** A screen; it is found by its name (ui_goto("photos")) */
struct nekobox_screen {
	const char *name;
	lv_obj_t *(*create)(void);
	void (*enter)(void *arg);	/**< every time the screen is shown; arg of ui_goto_arg() */
	void (*leave)(void);
};

#define NEKOBOX_SCREEN(_sym, _name, _create, _enter, _leave)                                         \
	const STRUCT_SECTION_ITERABLE(nekobox_screen, nekobox_screen_##_sym) = {                         \
		.name = (_name), .create = (_create), .enter = (_enter), .leave = (_leave),      \
	}

/**
 * A tile of the home screen. It opens the screen @c screen, or the file browser listing the
 * files of @c kind when @c screen is NULL (kind NULL: all files).
 */
struct nekobox_home_tile {
	int order;
	const char *symbol;
	const char *title;
	const char *subtitle;
	uint32_t color_top, color_bottom;
	const char *screen;
	const char *kind;
};

#define NEKOBOX_HOME_TILE(_sym, ...)                                                                 \
	const STRUCT_SECTION_ITERABLE(nekobox_home_tile, nekobox_tile_##_sym) = {__VA_ARGS__}

/** A row of the settings screen */
struct nekobox_settings_row {
	int order;
	void (*create)(lv_obj_t *list);
	void (*refresh)(void);		/**< once a second while the screen is shown; may be NULL */
};

#define NEKOBOX_SETTINGS_ROW(_sym, _order, _create, _refresh)                                        \
	const STRUCT_SECTION_ITERABLE(nekobox_settings_row, nekobox_row_##_sym) = {                      \
		.order = (_order), .create = (_create), .refresh = (_refresh),                   \
	}

/**
 * A kind of file the browser knows: which names belong to it, how it looks in the list and what
 * opens it. The browser only lists the files of a kind when it is opened for that kind.
 */
struct nekobox_file_handler {
	const char *kind;		/**< "video", "photo", "nes", "gb" */
	const char *title;		/**< title of the browser opened for this kind */
	const char *symbol;
	const uint32_t *color;		/**< a field of ui_pal, so that it follows the theme */
	const char *const *exts;	/**< lower case, with the dot, NULL terminated */
	void (*open)(const char *path);	/**< the file browser is the screen to return to */
	const char *extra_screen;	/**< a button in the title bar that opens this screen */
	const char *extra_label;
};

#define NEKOBOX_FILE_HANDLER(_sym, ...)                                                              \
	const STRUCT_SECTION_ITERABLE(nekobox_file_handler, nekobox_files_##_sym) = {__VA_ARGS__}

#endif /* NEKOBOX_UI_REGISTRY_H_ */

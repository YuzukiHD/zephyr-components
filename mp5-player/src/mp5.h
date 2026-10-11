/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MP5_H_
#define MP5_H_

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* ---- palette ---------------------------------------------------------------------------- */

#define C_BG		0x0B0F17
#define C_SURFACE	0x151B28
#define C_SURFACE2	0x1F2740
#define C_BORDER	0x2A3350
#define C_TEXT		0xE8ECF4
#define C_DIM		0x8B95A8
#define C_ACCENT	0x6C8CFF
#define C_ACCENT2	0x9B6CFF
#define C_OK		0x3DDC97
#define C_WARN		0xFFB547
#define C_PINK		0xFF6C9B

/* ---- screens ---------------------------------------------------------------------------- */

enum screen_id {
	SCR_HOME,
	SCR_FILES,
	SCR_PHOTOS,
	SCR_PLAYER,
	SCR_USB,
	SCR_RECENT,
	SCR_COUNT,
};

/** A plain screen in the background colour; transparent for the ones that show the video */
lv_obj_t *ui_screen_new(bool transparent);

/** A round, filled button with a symbol, used by the bars of the screens */
lv_obj_t *ui_icon_button(lv_obj_t *parent, const char *symbol, int32_t size);

/** A title bar with a back button and a title, at the top of @p parent */
lv_obj_t *ui_title_bar(lv_obj_t *parent, const char *title, lv_event_cb_t on_back);

/** Builds the screens and shows the home screen */
void ui_init(void);

/** Shows another screen */
void ui_goto(enum screen_id id);

/** The screens register themselves with these (ui.c calls them when the screen is shown) */
struct screen_ops {
	lv_obj_t *(*create)(void);
	void (*enter)(void *arg);
	void (*leave)(void);
};

void ui_register(enum screen_id id, const struct screen_ops *ops);
void ui_goto_arg(enum screen_id id, void *arg);

/** Plays a video; the back button of the player returns to @p from (SCR_FILES or SCR_RECENT) */
void ui_player_open(const char *path, enum screen_id from);

extern const struct screen_ops home_ops;
extern const struct screen_ops files_ops;
extern const struct screen_ops recent_ops;
extern const struct screen_ops player_ops;

/** Raises the CPU clock to CONFIG_SAMPLE_MP5_CPU_MHZ, each step checked */
void cpu_clock_setup(void);

/* ---- statistics, printed when they happen ---- */

void ui_stats_init(void);
void ui_stats_scroll_begin(const char *name);
void ui_stats_scroll_end(void);
void ui_stats_page_begin(const char *name);
void ui_stats_page_set_up(uint32_t us);
void ui_stats_demo(void);
void ui_stats_scroll_summary(void);

/** Scrolls the file list by @p dy pixels with the animation of a swipe (for the demo) */
void files_demo_scroll(int32_t dy);

/* ---- clock ------------------------------------------------------------------------------ */

/** The time of day; the board has no RTC, so it counts from the build time unless it is set */
void clock_now(struct tm *tm);
void clock_set(int hour, int minute);

/* ---- storage ---------------------------------------------------------------------------- */

bool storage_ready(void);
/** Total and free bytes of the SD card; false when it is not mounted */
bool storage_space(uint64_t *total, uint64_t *free_bytes);

#endif /* MP5_H_ */

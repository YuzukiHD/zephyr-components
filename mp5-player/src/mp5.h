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

/* The colours of the user interface: they change with the theme (dark or light, and the accent) */
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

/** The theme: dark or light and one of the accent colours */
#define UI_ACCENTS	6
bool ui_theme_dark(void);
int ui_theme_accent(void);
const char *ui_theme_accent_name(int index);
uint32_t ui_theme_accent_color(int index);
/** Brightness of the backlight, 10..255 */
int ui_brightness(void);
/** Sets the brightness; @p save also writes it to the settings */
void ui_brightness_set(int value, bool save);

/** Sets the theme, saves it and builds the screens again */
void ui_theme_set(bool dark, int accent);

/* ---- settings kept on the card ---------------------------------------------------------- */

/** Reads the settings file of the card (call after the card is mounted) */
void cfg_load(void);
/** The value of a key, NULL when it is not set */
const char *cfg_get(const char *key);
/** Sets a key (NULL deletes it) and writes the file */
void cfg_set(const char *key, const char *value);

/* ---- screens ---------------------------------------------------------------------------- */

enum screen_id {
	SCR_HOME,
	SCR_FILES,
	SCR_PHOTOS,
	SCR_PLAYER,
	SCR_USB,
	SCR_RECENT,
	SCR_GAME,
	SCR_SETTINGS,
	SCR_WIFI,
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

/** What the file browser lists besides the folders */
enum files_filter {
	FILES_ALL,
	FILES_NES,	/**< .nes */
	FILES_GB,	/**< .gb .gbc .gba */
	FILES_VIDEO,	/**< .mp4 .h264 .264 */
	FILES_PHOTO,	/**< .jpg .jpeg .png */
};

/** Opens the file browser in @p dir (NULL: the root of the card) listing only @p filter */
void ui_files_open(const char *dir, enum files_filter filter);

/** The dialog that sets the time of day; used by the home and the settings screen */
void ui_set_time_dialog(void);

/** Starts a game (.nes .gb .gbc .gba); the cross of the game screen returns to @p from */
void ui_game_open(const char *path, enum screen_id from);

/** Case blind comparison of file names */
int ui_name_cmp(const char *a, const char *b);

/** True for the file names of photos (.jpg .jpeg .png) */
bool ui_photo_ext(const char *name);

/** True for the file names of games */
bool ui_game_ext(const char *name);

extern const struct screen_ops home_ops;
extern const struct screen_ops files_ops;
extern const struct screen_ops recent_ops;
extern const struct screen_ops game_ops;
extern const struct screen_ops settings_ops;
extern const struct screen_ops wifi_ops;
extern const struct screen_ops photos_ops;
extern const struct screen_ops usb_ops;
extern const struct screen_ops player_ops;

/** Raises the CPU clock to CONFIG_SAMPLE_MP5_CPU_MHZ, each step checked */
void cpu_clock_setup(void);

/* ---- network ---- */

/** Starts the network worker; with a saved network it joins it and sets the clock */
void app_net_init(void);
/** One line about the Wi-Fi link and about the last time from the network, for the settings */
void app_net_status(char *buf, size_t size);
void app_net_sync_status(char *buf, size_t size);
/** Asks the network for the time and sets the clock with the answer */
void app_net_sync_time(void);
/** One line about Bluetooth: ready, or the device that is connected */
void bt_status(char *buf, size_t size);
/** True when a device is connected over Bluetooth */
bool bt_connected(void);

struct net_ap {
	char ssid[33];
	int8_t rssi;
	bool secured;
};

/** Starts a scan; the results come in while it runs */
void app_net_scan(void);
bool app_net_scanning(void);
/** Copies the access points found so far, strongest first; returns how many */
int app_net_scan_results(struct net_ap *out, int max);
/** The password saved for a network that was joined before; false when there is none */
bool app_net_saved_psk(const char *ssid, char *psk, size_t size);
/** Joins a network (psk may be empty for an open one) and saves it */
void app_net_connect(const char *ssid, const char *psk);
void app_net_disconnect(void);
bool app_net_connected(void);
/** True when the board has a Wi-Fi module that answered */
bool app_net_available(void);

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

/** The date and the time of day, from the real time clock */
void clock_now(struct tm *tm);
void clock_set(int hour, int minute);
/** Sets the date and the time (year since 1900, month 0..11) */
void clock_set_tm(const struct tm *tm);
/** False when the board has no usable real time clock */
bool clock_valid(void);

/* ---- storage ---------------------------------------------------------------------------- */

bool storage_ready(void);
/** Total and free bytes of the SD card; false when it is not mounted */
bool storage_space(uint64_t *total, uint64_t *free_bytes);

#endif /* MP5_H_ */

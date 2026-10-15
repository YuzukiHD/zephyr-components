/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_APP_H_
#define BILI_APP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "api/bili_parse.h"
#include "net/wifi.h"
#include "api/danmaku.h"

#define MAX_VIDEOS	100

enum list_id {
	LIST_RECOMMEND,
	LIST_POPULAR,
	LIST_FOLLOW,	/**< the videos of the followed users, only with a login */
	LIST_SEARCH,
	LIST_COUNT,
};

enum app_cmd_type {
	CMD_REFRESH,	/**< arg: list, load its first page again */
	CMD_MORE,	/**< arg: list, append the next page */
	CMD_SEARCH,	/**< keyword in the command: a new search */
	CMD_HOT,	/**< fetch the trending search words */
	CMD_PLAY,	/**< arg: list << 16 | index */
	CMD_STOP,	/**< back from the player */
	CMD_PAUSE,	/**< toggles */
	CMD_WIFI,	/**< join the network in ssid / psk */
	CMD_QR_START,	/**< ask for a new login code */
	CMD_QR_POLL,	/**< ask how the scan of the code is going */
	CMD_LOGOUT,	/**< forget the login */
	CMD_WIFI_SCAN,	/**< look for networks */
	CMD_SAVE_SETTINGS, /**< write volume and brightness to the card */
	CMD_USER_STATS,	/**< ask for the numbers of the account page */
	CMD_WIFI_DISCONNECT, /**< leave the network, the saved one stays */
	CMD_WIFI_FORGET, /**< leave the network and delete the saved one */
};

/** Posts a command to the worker thread; may be called from any thread */
void app_cmd(enum app_cmd_type type, int arg);
/** Joins a network; the name and password are kept on the card when it worked */
void app_cmd_wifi(const char *ssid, const char *psk);
void app_cmd_search(const char *keyword);
/** The name of the network that is saved on the card (and joined again at the start); false when there is none */
bool app_wifi_saved(char *ssid, size_t size);

/** What a list holds now; @p gen changes when the list was replaced (a refresh or a new search) */
struct list_info {
	int count;
	uint32_t gen;
	bool more;	/**< another page may exist */
	bool loading;	/**< the worker is fetching a page */
	int error;	/**< the last page request failed with this (0: it worked) */
};
void app_list_info(enum list_id list, struct list_info *info);

/* ---- the login ------------------------------------------------------------------------------ */

enum qr_state {
	QR_NONE,	/**< no code asked for */
	QR_WAIT,	/**< the code is shown, nobody scanned it */
	QR_SCANNED,	/**< scanned, waiting for the confirmation on the phone */
	QR_EXPIRED,	/**< the code is out of date */
	QR_DONE,	/**< logged in */
	QR_ERROR,	/**< the server could not be asked */
};

struct qr_info {
	enum qr_state state;
	char url[200];	/**< what the code shows */
	uint32_t gen;	/**< changes when there is a new code */
};
void app_qr_info(struct qr_info *info);

/** Whether a login is there; @p name gets the user name (empty until it is known) */
bool app_account(char *name, size_t size);

/**
 * The picture of the user (AVATAR_SIZE square RGB565, rows AVATAR_STRIDE bytes apart, 64 byte aligned);
 * false until it was fetched. The buffer stays the same, @p gen changes when its pixels do.
 */
#define AVATAR_SIZE	96
#define AVATAR_STRIDE	192
bool app_avatar(const uint16_t **px, uint32_t *gen);

/** The numbers of the account; false until they were fetched, @p gen changes with them */
bool app_user_stats(struct bili_user_stats *out, uint32_t *gen);

/** The networks of the last scan (strongest first); @p scanning is true while a scan runs */
int app_wifi_results(struct wifi_ap *out, int max, bool *scanning);

/** What malloc can still give: blocks of 64 KiB (all, in KiB) and the biggest single block (KiB) */
void app_mem_probe(unsigned int *free_kib, unsigned int *biggest_kib);

/** Copy of a video of a list, false when there is none */
bool app_video(enum list_id list, int index, struct bili_video *out);

/* ---- the user interface; the setters may be called from any thread ----------------------- */

void ui_init(void);
/** Runs in the thread that owns LVGL: applies what the setters stored and draws; returns the ms to the next call */
uint32_t ui_run(void);

/** For the self test: type keys on the search keyboard, pick a candidate (-1: none), press search */
void ui_type(const char *keys, int pick, bool submit_now);

/** Shows the home (0) or the search (1) page; for the self test */
void ui_goto(int page);

void ui_set_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void ui_set_hot_words(char words[][BILI_HOT_WORD_LEN], int count);
/** The comments of the video that is about to play; the UI takes the set over (NULL: none) */
void ui_set_danmaku(struct dm_set *set);

/** @p title NULL leaves the player screen */
void ui_set_playing(const char *title, uint32_t duration_ms);
void ui_set_position(uint32_t pos_ms, bool paused);
/** How much of the video is in the file (0..1000), for the progress bar of the player page */
void ui_set_buffered(int permille);

/** For the self test: scrolls the list of the home page by @p dy pixels; false at the end */
bool ui_home_scroll(int dy);
/** For the self test: selects the tab of the home page (0 recommend, 1 popular) */
void ui_home_tab(int tab);
/** For the self test: the page that is shown (0 home, 1 search, 2 player) */
int ui_current_page(void);

#endif /* BILI_APP_H_ */

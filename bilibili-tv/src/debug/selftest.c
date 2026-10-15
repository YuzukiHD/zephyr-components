/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The self test of the application, in the manner of the one of NekoBox: it is started by main(), driven by a
 * timer of the user interface thread, uses the controls the screen has (the list, the search, a tap on a
 * video) and prints what it finds, ending with PASS or FAIL.
 *
 *   - the home list is scrolled down to its end (the pages load by themselves) and up again, several
 *     times: what the arena and the LVGL pool hold must not grow;
 *   - searches for words with characters the keyboard has no key for (rare characters, an emoji, a
 *     space) must not fail;
 *   - the first videos are played: the position must move on, and after the stop and a short pause the
 *     memory is measured (the first round is the reference).
 *
 * A thread watches the test: when the timer has not run for 20 s it prints the state of every thread.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/arch/exception.h>
#include <zephyr/cache.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/fs/fs.h>
#include <zephyr/device.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "debug/profile.h"
#include "core/settings.h"
#include "lvgl_mem.h"
#include "ui/cover.h"
#include "player/player.h"
#include "ui/ui_priv.h"

#define TICK_MS		250
#define SCROLL_STEP	320
#define MEM_TOLERANCE	256U	/* KiB */

enum phase {
	P_LIST, P_BENCH, P_SCROLL, P_TAB, P_SEARCH, P_PAGES, P_PLAY, P_WIFI, P_DONE,
};

static const char *const names[] = {"list", "bench", "scroll", "popular", "search", "pages", "play", "wifi", "done"};
static const char *const words[] = {
	"\xe5\x85\x83\xe8\xba\xab",					/* a common word */
	"\xe9\xbe\x98",							/* a rare character */
	"\xf0\xa0\xae\xb7\xe9\x87\x8e\xe5\xae\xb6",			/* a character outside the BMP */
	"\xf0\x9f\x99\x82",						/* an emoji */
	"\xe5\x8e\x9f\xe7\xa5\x9e  \xe5\x90\xaf\xe5\x8a\xa8 & c++",	/* spaces and symbols */
	/* 40 Chinese characters: 120 bytes, more than a keyword holds */
	"\xe4\xb8\xad\xe5\x9b\xbd\xe5\x8e\x86\xe5\x8f\xb2\xe7\xba\xaa\xe5\xbd\x95\xe7\x89\x87\xe5\x85\xa8\xe9\x9b\x86\xe4\xb8\xad\xe5\x9b\xbd\xe5\x8e\x86\xe5\x8f\xb2\xe7\xba\xaa\xe5\xbd\x95\xe7\x89\x87\xe5\x85\xa8\xe9\x9b\x86\xe4\xb8\xad\xe5\x9b\xbd\xe5\x8e\x86\xe5\x8f\xb2\xe7\xba\xaa\xe5\xbd\x95\xe7\x89\x87\xe5\x85\xa8\xe9\x9b\x86\xe4\xb8\xad\xe5\x9b\xbd\xe5\x8e\x86\xe5\x8f\xb2\xe7\xba\xaa\xe5\xbd\x95\xe7\x89\x87\xe5\x85\xa8\xe9\x9b\x86",
};

static volatile enum phase phase;
static volatile int64_t last_tick;
static int failures, step, round_no;
static int64_t since;
static bool asked;
static unsigned int ref_free, ref_biggest, ref_lv_used;
static bool have_ref;
static int64_t cover_wait;	/* since when the test waits for the covers */

static void check(bool ok, const char *what)
{
	if (!ok) {
		failures++;
	}
	printk("selftest: %-52s %s\n", what, ok ? "ok" : "FAILED");
}

static void to(enum phase p)
{
	phase = p;
	step = 0;
	asked = false;
	since = k_uptime_get();
}

/* reference: remember the figures; compare: check against them (the lists and covers of the tabs and
 * searches are alive then, only the state after a video is expected to match) */
static void memory_at(const char *tag, bool reference, bool compare)
{
	unsigned int free_kib, biggest, lv_used, lv_peak, lv_free;
	struct sys_memory_stats st = {0};

	app_mem_probe(&free_kib, &biggest);
	lvgl_heap_stats(&st);
	lv_used = st.allocated_bytes / 1024;
	lv_peak = st.max_allocated_bytes / 1024;
	lv_free = st.free_bytes / 1024;
	printk("selftest: memory %-18s malloc free %5u KiB, biggest %5u KiB; LVGL heap used %4u KiB, peak %4u KiB, free %4u KiB\n",
	       tag, free_kib, biggest, lv_used, lv_peak, lv_free);
	if (reference) {
		ref_free = free_kib;
		ref_biggest = biggest;
		ref_lv_used = lv_used;
		have_ref = true;
	} else if (have_ref && compare) {
		check(free_kib + MEM_TOLERANCE >= ref_free, "the free memory is as at the reference");
		check(biggest + MEM_TOLERANCE >= ref_biggest, "the biggest block is as at the reference");
		check(lv_used <= ref_lv_used + MEM_TOLERANCE / 2, "the LVGL heap is as at the reference");
	}
	check(lv_free >= 64, "the LVGL heap has 64 KiB free");
}

static void memory(const char *tag, bool reference)
{
	memory_at(tag, reference, !reference);
}

static void render_event(lv_event_t *e);

static void list_phase(void)
{
	struct list_info info;

	app_list_info(LIST_RECOMMEND, &info);
	if (info.count > 0) {
		printk("selftest: the recommended list has %d videos\n", info.count);
		check(true, "the recommended list loaded");
		memory("start", false);
		round_no = 0;
		lv_display_add_event_cb(lv_display_get_default(), render_event, LV_EVENT_RENDER_START, NULL);
		lv_display_add_event_cb(lv_display_get_default(), render_event, LV_EVENT_RENDER_READY, NULL);
		to(IS_ENABLED(CONFIG_BILI_SELFTEST_WIFI_ONLY) ? P_WIFI :
		   IS_ENABLED(CONFIG_BILI_SELFTEST_PLAY_ONLY) ? P_PLAY : P_BENCH);
	} else if (k_uptime_get() - since > 120000) {
		check(false, "the recommended list loaded");
		to(P_SEARCH);
	}
}

/* down to the end of the list (and the pages it loads), up to its top */
/* the time LVGL needs to draw a frame, over the scrolling */
static uint32_t render_t0, render_n, render_sum, render_max;

static void render_event(lv_event_t *e)
{
	if (lv_event_get_code(e) == LV_EVENT_RENDER_START) {
		render_t0 = k_cycle_get_32();
	} else if (render_t0 != 0) {
		uint32_t us = (uint32_t)((uint64_t)(k_cycle_get_32() - render_t0) * 1000000U /
					 sys_clock_hw_cycles_per_sec());

		render_n++;
		render_sum += us;
		render_max = MAX(render_max, us);
		render_t0 = 0;
	}
}

/*
 * The time of the scrolling alone: once the covers of the first page are there the list is moved
 * down and up in small steps, one for every round of the interface, and the time of the frames, the
 * share of every thread and the hot spots of the interface thread are printed.
 */
static uint8_t *saved;

static void bench_phase(void)
{
	static int moves;
	static bool down;

	if (step == 0) {
		if (cover_pending() == 0 && k_uptime_get() - since > 4000) {
			render_n = render_sum = render_max = 0;
			moves = 0;
			down = true;
			prof_start();
			step = 1;
		}
		return;
	}
	if (step == 1) {
		/* 48 pixels a round; down as far as the loaded page goes, then back */
		if (down) {
			down = ui_home_scroll(48);
		} else if (!ui_home_scroll(-48)) {
			prof_report("scrolling the first page down and up");
			printk("selftest: scrolling: %u frames, mean %u us, worst %u us, %d moves\n", render_n,
			       render_n ? render_sum / render_n : 0, render_max, moves);
			{
				char st[600];

				ui_scroll_stats(st, sizeof(st));
				printk("selftest: scroll events: %s\n", st);
			}
			render_n = render_sum = render_max = 0;
			ui_home_scroll(-100000);
			step = 2;
			since = k_uptime_get();
			return;
		}
		moves++;
	} else if (step == 2 && k_uptime_get() - since > 2500 && (cover_pending() == 0 || k_uptime_get() - since > 40000)) {
		/* what is on the screen after all the moves; then everything is drawn again and compared */
		const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
		uint32_t stride;
		uint8_t *fb = display_sunxi_framebuffer(disp, &stride);

		saved = malloc((size_t)614 * stride);
		if (saved == NULL) {
			round_no = 0;
			to(P_SCROLL);
			return;
		}
		sys_cache_data_invd_range(fb + 124 * stride, (size_t)614 * stride);
		memcpy(saved, fb + 124 * stride, (size_t)614 * stride);
		lv_obj_invalidate(lv_screen_active());
		step = 3;
		since = k_uptime_get();
	} else if (step == 3 && k_uptime_get() - since > 3000 && cover_pending() == 0) {
		const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
		uint32_t stride, diff = 0, total = 1024 * 614, band[39] = {0}, cols[8] = {0};
		uint8_t *fb = display_sunxi_framebuffer(disp, &stride);
		int first_y = -1, first_x = -1;

		sys_cache_data_invd_range(fb + 124 * stride, (size_t)614 * stride);
		for (int y = 0; y < 614; y++) {
			const uint32_t *a = (const uint32_t *)(saved + (size_t)y * stride);
			const uint32_t *b = (const uint32_t *)(fb + (size_t)(124 + y) * stride);

			for (int x = 0; x < 1024; x++) {
				/* a pixel that is not seen has no colour that matters */
				if (a[x] != b[x] && !((a[x] >> 24) == 0 && (b[x] >> 24) == 0)) {
					diff++;
					band[y / 16]++;
					cols[x / 128]++;
					if (first_y < 0) {
						first_y = y;
						first_x = x;
						printk("selftest: first difference at %d,%d: %08x against %08x\n", x, 124 + y,
						       (unsigned)a[x], (unsigned)b[x]);
					}
				}
			}
		}
		free(saved);
		saved = NULL;
		printk("selftest: differing pixels per 16 rows from the top:");
		for (int i = 0; i < 39; i++) {
			printk(" %u", band[i]);
		}
		printk("\nselftest: per 128 columns:");
		for (int i = 0; i < 8; i++) {
			printk(" %u", cols[i]);
		}
		printk("\n");
		printk("selftest: after the moves %u of %u pixels differ from a full redraw\n", diff, total);
		check(diff == 0, "scrolling by moving the picture leaves what a full redraw gives");
		round_no = 0;
		to(P_SCROLL);
	}
}

static void scroll_phase(void)
{
	struct list_info info;
	char tag[32];

	app_list_info(LIST_RECOMMEND, &info);
	if (step == 0) {
		/* down; the next step waits until the covers that are on the way have arrived */
		if (cover_pending() > 0 && k_uptime_get() - cover_wait < 30000) {
			return;
		}
		cover_wait = k_uptime_get();
		if (ui_home_scroll(SCROLL_STEP)) {
			since = k_uptime_get();
		} else if (info.more && k_uptime_get() - since < 30000) {
			/* the page that the end asks for is on its way */
		} else {
			printk("selftest: scrolled to the end of %d videos\n", info.count);
			step = 1;
		}
	} else if (step == 1) {
		if (!ui_home_scroll(-4 * SCROLL_STEP)) {
			step = 2;
			since = k_uptime_get();
		}
	} else if (k_uptime_get() - since > 4000) {
		snprintf(tag, sizeof(tag), "scroll round %d", round_no + 1);
		/* the first round pays for the covers and the cards: it is the reference of the others */
		memory(tag, round_no == 0);
		printk("selftest: drawing: %u frames, mean %u us, worst %u us\n", render_n,
		       render_n ? render_sum / render_n : 0, render_max);
		render_n = render_sum = render_max = 0;
		if (++round_no >= CONFIG_BILI_SELFTEST_ROUNDS) {
			check(info.count > 20, "the list holds more than the first page");
			to(P_TAB);
		} else {
			step = 0;
			since = k_uptime_get();
		}
	}
}

/* the second tab as well: both lists have their cards at the same time */
static void tab_phase(void)
{
	struct list_info info;

	app_list_info(LIST_POPULAR, &info);
	if (step == 0) {
		ui_home_tab(1);
		step = 1;
		since = k_uptime_get();
	} else if (step == 1) {
		/* down to the end of the popular list, as the first one */
		if (cover_pending() > 0 && k_uptime_get() - cover_wait < 30000) {
			return;
		}
		cover_wait = k_uptime_get();
		if (ui_home_scroll(SCROLL_STEP)) {
			since = k_uptime_get();
		} else if ((info.more || info.count == 0) && k_uptime_get() - since < 30000) {
			/* the page is on its way */
		} else {
			printk("selftest: scrolled to the end of %d popular videos\n", info.count);
			check(info.count > 0, "the popular list loaded");
			step = 2;
			since = k_uptime_get();
		}
	} else if (k_uptime_get() - since > 3000) {
		memory_at("both tabs built", false, false);
		ui_home_tab(0);
		to(P_SEARCH);
	}
}

/* the account page gets a login code, the settings page finds networks and changes volume and brightness */
static void pages_phase(void)
{
	struct qr_info qi;
	struct wifi_ap found[16];
	bool scanning;
	int n;

	if (step == 0) {
		ui_home_tab(3);	/* the account tab */
		step = 1;
		since = k_uptime_get();
	} else if (step == 1) {
		app_qr_info(&qi);
		if (app_account(NULL, 0)) {
			/* a login saved on the card: there is no code to ask for */
			printk("selftest: already logged in\n");
			check(true, "the account page got a login code");
			ui_home_tab(4);	/* the settings tab */
			step = 2;
			since = k_uptime_get();
		} else if (qi.state == QR_WAIT && qi.url[0] != '\0') {
			printk("selftest: login code %s\n", qi.url);
			check(true, "the account page got a login code");
			ui_home_tab(4);	/* the settings tab */
			step = 2;
			since = k_uptime_get();
		} else if (k_uptime_get() - since > 20000) {
			check(false, "the account page got a login code");
			ui_home_tab(4);
			step = 2;
			since = k_uptime_get();
		}
	} else if (step == 2) {
		n = app_wifi_results(found, ARRAY_SIZE(found), &scanning);
		if (!scanning && (n > 0 || k_uptime_get() - since > 15000)) {
			printk("selftest: %d networks, the strongest %s\n", n, n > 0 ? found[0].ssid : "-");
			check(n > 0, "the settings page found networks");
			settings_set_volume(55);
			settings_set_brightness(120);
			check(settings_volume() == 55 && settings_brightness() == 120, "volume and brightness are set");
			app_cmd(CMD_SAVE_SETTINGS, 0);
			settings_set_brightness(200);
			ui_home_tab(0);
			to(P_PLAY);
		}
	}
}

static void search_phase(void)
{
	struct list_info info;
	static uint32_t gen;
	char what[96];

	if (step >= ARRAY_SIZE(words)) {
		memory_at("after the searches", false, false);
		to(P_PAGES);
		return;
	}
	if (!asked) {
		app_list_info(LIST_SEARCH, &info);
		gen = info.gen;
		app_cmd_search(words[step]);
		asked = true;
		since = k_uptime_get();
		return;
	}
	app_list_info(LIST_SEARCH, &info);
	if ((!info.loading && (info.gen != gen || info.error != 0) && k_uptime_get() - since > 1500) ||
	    k_uptime_get() - since > 40000) {
		snprintf(what, sizeof(what), "search %d answers without an error (%d, %d videos)", step + 1, info.error,
			 info.count);
		check(info.error == 0 && !info.loading, what);
		step++;
		asked = false;
	}
}

/*
 * The connection of the board, as the buttons of the settings do it: leave the network, join the saved
 * one again, a wrong password (the join fails, the saved network stays), forget the saved network
 * (the file goes) and save it again by joining with the same name and password.
 */
static char wifi_name[33], wifi_pass[65];

static void wifi_phase(void)
{
	char name[33];
	int64_t now = k_uptime_get();
	struct fs_dirent st;
	bool timeout = now - since > 70000;

	switch (step) {
	case 0: {
		struct fs_file_t f;
		char buf[110] = "";
		char *nl;

		if (wifi_state() != WIFI_CONNECTED && !timeout) {
			break;
		}
		check(wifi_state() == WIFI_CONNECTED, "the board is connected at the start");
		fs_file_t_init(&f);
		if (fs_open(&f, "/SD:/wifi.cfg", FS_O_READ) == 0) {
			fs_read(&f, buf, sizeof(buf) - 1);
			fs_close(&f);
		}
		nl = strchr(buf, '\n');
		if (nl != NULL) {
			*nl++ = '\0';
			nl[strcspn(nl, "\r\n")] = '\0';
			snprintf(wifi_pass, sizeof(wifi_pass), "%s", nl);
		}
		snprintf(wifi_name, sizeof(wifi_name), "%s", buf);
		check(wifi_name[0] != '\0' && app_wifi_saved(name, sizeof(name)), "a network is saved");
		app_cmd(CMD_WIFI_DISCONNECT, 0);
		step = 1;
		since = now;
		break;
	}
	case 1:
		if (wifi_state() == WIFI_DISCONNECTED || now - since > 15000) {
			check(wifi_state() == WIFI_DISCONNECTED && wifi_current()[0] == '\0', "disconnect leaves the network");
			check(app_wifi_saved(name, sizeof(name)), "disconnect keeps the saved network");
			app_cmd(CMD_WIFI, 0);
			step = 2;
			since = now;
		}
		break;
	case 2:
		if (wifi_state() == WIFI_CONNECTED || now - since > 60000) {
			check(wifi_state() == WIFI_CONNECTED && strcmp(wifi_current(), wifi_name) == 0, "the saved network is joined again");
			app_cmd_wifi(wifi_name, "not the password");
			step = 3;
			since = now;
		}
		break;
	case 3:
		if (wifi_state() == WIFI_FAILED || now - since > 70000) {
			check(wifi_state() == WIFI_FAILED, "a wrong password fails");
			check(app_wifi_saved(name, sizeof(name)) && strcmp(name, wifi_name) == 0, "the failure keeps the saved network");
			app_cmd(CMD_WIFI, 0);
			step = 4;
			since = now;
		}
		break;
	case 4:
		if (wifi_state() == WIFI_CONNECTED || now - since > 60000) {
			check(wifi_state() == WIFI_CONNECTED, "the saved network works after the failure");
			app_cmd(CMD_WIFI_FORGET, 0);
			step = 5;
			since = now;
		}
		break;
	case 5:
		if ((wifi_state() == WIFI_DISCONNECTED && !app_wifi_saved(name, sizeof(name))) || now - since > 20000) {
			check(wifi_state() == WIFI_DISCONNECTED, "forget leaves the network");
			check(!app_wifi_saved(name, sizeof(name)), "forget removes the saved network");
			check(fs_stat("/SD:/wifi.cfg", &st) != 0, "forget deletes the file");
			app_cmd_wifi(wifi_name, wifi_pass);
			step = 6;
			since = now;
		}
		break;
	case 6:
		/* the name is saved a moment after the join has ended */
		if ((wifi_state() == WIFI_CONNECTED && app_wifi_saved(name, sizeof(name))) || now - since > 60000) {
			check(wifi_state() == WIFI_CONNECTED, "the network is joined with its password");
			check(app_wifi_saved(name, sizeof(name)) && strcmp(name, wifi_name) == 0, "joining saves the network again");
			check(fs_stat("/SD:/wifi.cfg", &st) == 0, "the file is back");
			check(failures == 0, "no step failed");
			printk("selftest: %s\n", failures == 0 ? "PASS (0 failures)" : "FAIL");
			to(P_DONE);
		}
		break;
	}
}

/* plays the first videos for a while: the position has to move */
static void play_phase(void)
{
	static uint32_t pos0;
	static int64_t t0;
	static int index;
	char what[96];
	int64_t now = k_uptime_get();

	if (index >= CONFIG_BILI_SELFTEST_PLAYS) {
		check(failures == 0, "no step failed");
		printk("selftest: %s\n", failures == 0 ? "PASS (0 failures)" : "FAIL");
		to(P_DONE);
		return;
	}
	if (step == 0) {
		enum list_id list = IS_ENABLED(CONFIG_BILI_SELFTEST_PLAY_POPULAR) ? LIST_POPULAR : LIST_RECOMMEND;
		struct list_info li;

		app_list_info(list, &li);
		if (li.count == 0) {
			/* the list of the tab is only loaded when it is looked at */
			static bool asked;

			if (!asked) {
				asked = true;
				app_cmd(CMD_REFRESH, list);
			}
			return;
		}
		app_cmd(CMD_PLAY, list << 16 | (index % MIN(li.count, 20)));
		printk("selftest: playing video %d\n", index);
		step = 1;
		since = now;
	} else if (step == 1) {
		/* the player is up and shows a picture */
		if (player_state() == PLAYER_PLAYING && player_position_ms() > 1500) {
			pos0 = player_position_ms();
			t0 = now;
			step = 2;
		} else if (now - since > 90000) {
			snprintf(what, sizeof(what), "video %d starts (state %d)", index, (int)player_state());
			check(false, what);
			app_cmd(CMD_STOP, 0);
			step = 3;
			since = now;
		}
	} else if (step == 2) {
		/* pause for a moment and go on: the state must follow and the position move again */
		static int pause_phase;
		static int64_t pause_at;
		static uint32_t pause_pos;
		static int pause_toggles;

		if (pause_phase == 0 && now - t0 >= 3000) {
			app_cmd(CMD_PAUSE, 0);
			pause_at = now;
			pause_phase = 1;
		} else if (pause_phase == 1 && now - pause_at >= 2500) {
			check(player_state() == PLAYER_PAUSED, "the player is paused");
			pause_pos = player_position_ms();
			app_cmd(CMD_PAUSE, 0);
			pause_at = now;
			pause_phase = 2;
		} else if (pause_phase == 2 && now - pause_at >= 3000) {
			check(player_state() == PLAYER_PLAYING && player_position_ms() > pause_pos + 1500,
			      "the player plays on after the pause");
			pause_phase = 3;
			pause_at = now;
			pause_toggles = 0;
		} else if (pause_phase == 3 && now - pause_at >= 350) {
			/* quick taps on the button */
			app_cmd(CMD_PAUSE, 0);
			pause_at = now;
			if (++pause_toggles >= 9) {
				pause_phase = 4;
			}
		} else if (pause_phase == 4 && now - pause_at >= 4000) {
			if (player_state() == PLAYER_PAUSED) {
				app_cmd(CMD_PAUSE, 0);
			}
			pause_phase = 5;
			pause_at = now;
		} else if (pause_phase == 5 && now - pause_at >= 3000) {
			check(player_state() == PLAYER_PLAYING, "the player plays after quick pause taps");
			pause_phase = 6;
			pos0 = player_position_ms();
			t0 = now;
		}
		if (pause_phase == 6 && now - t0 >= CONFIG_BILI_SELFTEST_PLAY_SECONDS * 1000) {
			uint32_t moved = player_position_ms() - pos0;

			snprintf(what, sizeof(what), "video %d plays on (%u ms in %d s)", index, moved, CONFIG_BILI_SELFTEST_PLAY_SECONDS);
			check(player_state() == PLAYER_PLAYING && moved > CONFIG_BILI_SELFTEST_PLAY_SECONDS * 750, what);
			pause_phase = 0;
			app_cmd(CMD_STOP, 0);
			step = 3;
			since = now;
		}
	} else if (step == 3) {
		if (ui_current_page() == 0 && player_state() == PLAYER_IDLE) {
			step = 4;
			since = now;
		} else if (now - since > 30000) {
			check(false, "the player page is left after the stop");
			step = 4;
			since = now;
		}
	} else if (now - since > 4000) {
		snprintf(what, sizeof(what), "after video %d", index);
		memory(what, index == 0);
		index++;
		step = 0;
	}
}

static void tick(lv_timer_t *t)
{
	last_tick = k_uptime_get();
	switch (phase) {
	case P_LIST:
		list_phase();
		break;
	case P_BENCH:
		bench_phase();
		break;
	case P_SCROLL:
		scroll_phase();
		break;
	case P_TAB:
		tab_phase();
		break;
	case P_SEARCH:
		search_phase();
		break;
	case P_PAGES:
		pages_phase();
		break;
	case P_PLAY:
		play_phase();
		break;
	case P_WIFI:
		wifi_phase();
		break;
	default:
		lv_timer_delete(t);
		break;
	}
}

static void show_thread(const struct k_thread *th, void *user)
{
	size_t unused = 0;
	k_thread_runtime_stats_t st = {0};

	k_thread_stack_space_get(th, &unused);
	k_thread_runtime_stats_get((k_tid_t)th, &st);
	printk("  %-20s prio %3d %-24s stack unused %5u, cpu %llu Mcycles",
	       k_thread_name_get((k_tid_t)th) ? k_thread_name_get((k_tid_t)th) : "?", k_thread_priority_get((k_tid_t)th),
	       k_thread_state_str((k_tid_t)th, (char[24]){0}, 24), (unsigned int)unused,
	       (unsigned long long)(st.execution_cycles / 1000000ULL));
	/* a thread that an interrupt took off the CPU has its registers on its stack: where it was */
	{
		uintptr_t sp = (uintptr_t)th->callee_saved.sp;

		printk(", saved sp %p ra %p", (void *)sp, (void *)th->callee_saved.ra);
		if (sp >= th->stack_info.start && sp < th->stack_info.start + th->stack_info.size) {
			const struct arch_esf *e = (const void *)sp;

			printk(", mepc %p esf.ra %p", (void *)e->mepc, (void *)e->ra);
		}
	}
	printk("\n");
}

/* when the timer of the interface does not run any more, the threads are listed */
static void watch_main(void *a, void *b, void *c)
{
	for (;;) {
		k_sleep(K_SECONDS(10));
		if (phase != P_DONE && k_uptime_get() - last_tick > 20000) {
			printk("selftest: HANG: the interface timer has not run for %lld s in the phase %s, step %d\n",
			       (long long)((k_uptime_get() - last_tick) / 1000), names[phase], step);
			printk("selftest: player state %d, position %u ms, page %d\n", (int)player_state(),
			       player_position_ms(), ui_current_page());
			k_thread_foreach(show_thread, NULL);
			{
				k_thread_runtime_stats_t all;

				k_thread_runtime_stats_all_get(&all);
				printk("selftest: cpu in total %llu Mcycles, idle %llu Mcycles\n",
				       (unsigned long long)(all.execution_cycles / 1000000ULL),
				       (unsigned long long)(all.idle_cycles / 1000000ULL));
			}
		}
	}
}

K_THREAD_DEFINE(selftest_watch, 3072, watch_main, NULL, NULL, NULL, 1, 0, 0);

void bili_selftest_start(void)
{
	last_tick = k_uptime_get();
	since = last_tick;
	printk("selftest: start, %d scroll rounds, %d videos\n", CONFIG_BILI_SELFTEST_ROUNDS,
	       CONFIG_BILI_SELFTEST_PLAYS);
	lv_timer_create(tick, TICK_MS, NULL);
}

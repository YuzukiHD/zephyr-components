/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Self check of the theme: on the settings screen it switches between dark and light and through
 * the accent colours, as the switch and the dots of the screen do, and prints the palette and what
 * was saved. It ends with PASS or FAIL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "core/cfg.h"
#include "core/storage.h"
#include "features/games/game.h"
#include "features/net/net.h"
#include "features/video/player.h"
#include "ui/ui.h"

static int step;
static int failures;

static void check(bool ok, const char *what)
{
	if (!ok) {
		failures++;
	}
	printk("theme: %-44s %s\n", what, ok ? "ok" : "FAILED");
}

/* the steps of a feature that is not built are left out; the dead calls are not linked */
static bool net_up(void)
{
	return IS_ENABLED(CONFIG_MP5_FEATURE_NET) && app_net_connected();
}

static void test_step(lv_timer_t *t)
{
	const char *mode = cfg_get("theme");
	const char *acc = cfg_get("accent");
	static const bool dark[] = {false, false, true, true, false};
	static const int accent[] = {0, 3, 3, 5, 0};

	/* first what the previous step asked for, then the next request */
	if (step >= 2 && step <= 6) {
		int i = step - 2;

		check(ui_theme_dark() == dark[i] && ui_theme_accent() == accent[i],
		      "palette follows the request");
		check(ui_pal.accent == ui_theme_accent_color(accent[i]), "accent color in the palette");
		check(ui_pal.bg == (dark[i] ? 0x0B0F17U : 0xF1F3F9U), "background of the mode");
		check(mode != NULL && strcmp(mode, dark[i] ? "dark" : "light") == 0 &&
		      acc != NULL && atoi(acc) == accent[i], "saved in the settings file");
		check(lv_screen_active() != NULL && lv_obj_get_child_count(lv_screen_active()) > 0,
		      "screen rebuilt and active");
	}
	if (step == 0) {
		ui_goto("settings");
	} else if (step <= 5) {
		ui_theme_set(dark[step - 1], accent[step - 1]);
	} else if (step == 6) {
		char psk[65];
		int old = ui_brightness();

		ui_brightness_set(120, true);
		check(ui_brightness() == 120 && cfg_get("brightness") != NULL &&
		      atoi(cfg_get("brightness")) == 120, "brightness set and saved");
		ui_brightness_set(old, true);
		check(ui_brightness() == old, "brightness restored");
		if (net_up()) {
			check(app_net_saved_psk("ChinaNet-K5ii", psk, sizeof(psk)) && strlen(psk) > 0,
			      "password of the joined network is kept");
			check(!app_net_saved_psk("no such network", psk, sizeof(psk)),
			      "no password for an unknown network");
		} else {
			printk("theme: not online, the saved password is not checked\n");
		}
	} else if (step == 7) {
		/* the Wi-Fi screen scans for networks while the board is online */
		ui_goto("wifi");
		/* what the Scan button does */
		if (IS_ENABLED(CONFIG_MP5_FEATURE_NET)) {
			app_net_scan();
		}
	} else if (step >= 10 && step <= 16) {
		/* the Scan button again and again, with the list made again each time */
		if (IS_ENABLED(CONFIG_MP5_FEATURE_NET)) {
			app_net_scan();
		}
	} else if (step == 9) {
		struct net_ap list[24];
		char psk[65];

		/* the same network again, as a second tap on it */
		if (net_up() && app_net_saved_psk("ChinaNet-K5ii", psk, sizeof(psk))) {
			printk("theme: connecting to the network that is joined already\n");
			app_net_connect("ChinaNet-K5ii", psk);
		}
		if (IS_ENABLED(CONFIG_MP5_FEATURE_NET)) {
			int n = app_net_scan_results(list, ARRAY_SIZE(list));

			printk("theme: the Wi-Fi screen found %d networks\n", n);
			check(n > 0, "scan while online lists networks");
		}
	} else if (step == 8) {
		/* wait for the scan */
	} else if (step == 17) {
		ui_goto("settings");
	} else if (step == 18) {
		/* a game with sound, then leaving it: the amplifier must go off */
		if (IS_ENABLED(CONFIG_MP5_FEATURE_GAMES)) {
			ui_game_open("/SD:/Legend-of-Zelda.gb", "home");
		}
	} else if (step == 21) {
		ui_goto("home");
	} else if (step == 19 || step == 20) {
		/* the game runs */
	} else if (step == 23) {
		check(failures == 0, "no step failed");
		printk("theme: %s\n", failures == 0 ? "PASS (0 failures)" : "FAIL");
		lv_timer_delete(t);
		return;
	}
	if (step > 23) {
		return;
	}
	step++;
}

#ifdef CONFIG_MP5_THEME_TEST
static int theme_test_start(void)
{
	lv_timer_create(test_step, 2500, NULL);

	return 0;
}
MP5_INIT(MP5_STAGE_READY, 10, "theme test", theme_test_start);
#endif

#ifdef CONFIG_MP5_SELFTEST
/* The files in the root of the card, to see what there is to play */
static void list_root(void)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;

	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, MP5_MOUNT_POINT) != 0) {
		return;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
		printk("sd: %s %s %u\n", ent.type == FS_DIR_ENTRY_DIR ? "dir " : "file", ent.name,
		       (unsigned int)ent.size);
	}
	fs_closedir(&dir);
}

/* Drives the player the way the controls do and prints where it is after each step */
static void playback_main(void *a, void *b, void *c)
{
	static const char *const names[] = {"idle", "playing", "paused", "ended", "error"};
	static const struct {
		const char *what;
		int wait_ms;
	} steps[] = {
		{"play", 2500}, {"seek 6000", 2500}, {"pause", 1500}, {"seek 1000 while paused", 1500},
		{"resume", 2500}, {"volume 30", 1500}, {"volume 0", 1000}, {"volume 70", 500},
		{"seek 9500", 4000}, {"replay", 2000},
	};

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	k_msleep(1500);
	for (int i = 0; i < ARRAY_SIZE(steps); i++) {
		const char *w = steps[i].what;

		if (strcmp(w, "seek 6000") == 0) {
			player_seek(6000);
		} else if (strcmp(w, "pause") == 0) {
			player_pause();
		} else if (strcmp(w, "seek 1000 while paused") == 0) {
			player_seek(1000);
		} else if (strcmp(w, "resume") == 0) {
			player_play();
		} else if (strcmp(w, "volume 30") == 0) {
			player_set_volume(30);
		} else if (strcmp(w, "volume 0") == 0) {
			player_set_volume(0);
		} else if (strcmp(w, "volume 70") == 0) {
			player_set_volume(70);
		} else if (strcmp(w, "seek 9500") == 0) {
			player_seek(9500);
		} else if (strcmp(w, "replay") == 0) {
			player_play();
		}
		printk("selftest: %-24s -> ", w);
		k_msleep(steps[i].wait_ms);
		printk("%s, position %u ms, volume %d\n", names[player_state()], player_position_ms(),
		       player_volume());
	}
	printk("selftest: done\n");
}

K_THREAD_DEFINE(selftest, 4096, playback_main, NULL, NULL, NULL, 9, 0, -1);

static int playback_start(void)
{
	list_root();
	ui_goto("player");
	k_thread_start(selftest);

	return 0;
}
MP5_INIT(MP5_STAGE_READY, 5, "playback test", playback_start);
#endif

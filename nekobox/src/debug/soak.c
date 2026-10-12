/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Plays every video and every game in the root of the card, one after another, going back to the
 * home screen after each, and prints how much of the malloc arena is free before and after. The
 * list is gone through three times. The first round allocates what is allocated once and kept
 * (the display, the decoder, the first use of each emulator); what the end of the first round
 * leaves free is the reference, a leak shows as a smaller figure after the last round, a block
 * that stays in the middle of the arena as a smaller largest block.
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "core/storage.h"
#include "debug/memcheck.h"
#include "features/games/game.h"
#include "features/video/ui_player.h"
#include "ui/ui.h"

#define MAX_FILES	40
#define ROUNDS		3
#define VIDEO_SECONDS	8
#define GAME_SECONDS	10
#define SETTLE_SECONDS	3
#define TOLERANCE	(8U * 1024)

enum kind { K_VIDEO, K_GAME };

static struct {
	char path[96];
	enum kind kind;
} files[MAX_FILES];
static int nfiles, idx, round_no, phase, ticks;
static size_t base_largest, base_total;
static int base_total_round;
static int failures;

static bool ext_is(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);

	if (n <= e) {
		return false;
	}
	for (size_t i = 0; i < e; i++) {
		char c = name[n - e + i];

		if (c >= 'A' && c <= 'Z') {
			c += 'a' - 'A';
		}
		if (c != ext[i]) {
			return false;
		}
	}

	return true;
}

static void list_files(void)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;
	static const char *const video[] = {".mp4", ".h264"};
	static const char *const game[] = {".gb", ".gbc", ".nes", ".gba"};

	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, NEKOBOX_MOUNT_POINT) != 0) {
		return;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0' && nfiles < MAX_FILES) {
		if (ent.type != FS_DIR_ENTRY_FILE || ent.name[0] == '.') {
			continue;
		}
		for (int i = 0; i < ARRAY_SIZE(video); i++) {
			if (IS_ENABLED(CONFIG_NEKOBOX_FEATURE_VIDEO) && ext_is(ent.name, video[i])) {
				files[nfiles].kind = K_VIDEO;
				snprintf(files[nfiles++].path, sizeof(files[0].path), "%s/%s",
					 NEKOBOX_MOUNT_POINT, ent.name);
				goto next;
			}
		}
		for (int i = 0; i < ARRAY_SIZE(game); i++) {
			if (IS_ENABLED(CONFIG_NEKOBOX_FEATURE_GAMES) && ext_is(ent.name, game[i])) {
				files[nfiles].kind = K_GAME;
				snprintf(files[nfiles++].path, sizeof(files[0].path), "%s/%s",
					 NEKOBOX_MOUNT_POINT, ent.name);
				break;
			}
		}
next:
		;
	}
	fs_closedir(&dir);
}

static void report(const char *tag, const char *path)
{
	size_t largest, total;

	nekobox_mem_probe(&largest, &total);
	printk("soak: round %d %-6s %-32s largest block %6u KiB, free in total %6u KiB\n", round_no + 1,
	       tag, path, (unsigned)(largest / 1024), (unsigned)(total / 1024));
}

static void open_file(int i)
{
	if (files[i].kind == K_VIDEO) {
#ifdef CONFIG_NEKOBOX_FEATURE_VIDEO
		ui_player_open(files[i].path, "home");
#endif
	} else {
#ifdef CONFIG_NEKOBOX_FEATURE_GAMES
		ui_game_open(files[i].path, "home");
#endif
	}
}

static void step(lv_timer_t *t)
{
	if (phase == 0) {
		if (idx >= nfiles) {
			idx = 0;
			round_no++;
		}
		if (round_no == 1 && base_total_round == 0) {
			nekobox_mem_probe(&base_largest, &base_total);
			base_total_round = 1;
			printk("soak: reference after the first round: largest block %u KiB, free in total %u KiB\n",
			       (unsigned)(base_largest / 1024), (unsigned)(base_total / 1024));
		}
		if (round_no >= ROUNDS) {
			size_t largest, total;
			bool ok;

			nekobox_mem_probe(&largest, &total);
			ok = largest + TOLERANCE >= base_largest && total + TOLERANCE >= base_total;
			printk("soak: reference: largest %u KiB, total %u KiB; end: largest %u KiB, total %u KiB\n",
			       (unsigned)(base_largest / 1024), (unsigned)(base_total / 1024),
			       (unsigned)(largest / 1024), (unsigned)(total / 1024));
			failures += ok ? 0 : 1;
			printk("soak: %s\n", failures == 0 ? "PASS (0 failures)" : "FAIL");
			lv_timer_delete(t);
			return;
		}
		printk("soak: round %d, opening %s\n", round_no + 1, files[idx].path);
		open_file(idx);
		phase = 1;
		ticks = 0;
	} else if (phase == 1) {
		int limit = files[idx].kind == K_VIDEO ? VIDEO_SECONDS : GAME_SECONDS;

		if (++ticks >= limit) {
			ui_goto("home");
			phase = 2;
			ticks = 0;
		}
	} else if (++ticks >= SETTLE_SECONDS) {
		report("after", files[idx].path);
		idx++;
		phase = 0;
	}
}

static int soak_start(void)
{
	list_files();
	printk("soak: %d files to play\n", nfiles);
	if (nfiles == 0) {
		return 0;
	}
	nekobox_mem_probe(&base_largest, &base_total);
	printk("soak: start: largest block %u KiB, free in total %u KiB\n",
	       (unsigned)(base_largest / 1024), (unsigned)(base_total / 1024));
	lv_timer_create(step, 1000, NULL);

	return 0;
}
NEKOBOX_INIT(NEKOBOX_STAGE_READY, 20, "memory soak", soak_start);

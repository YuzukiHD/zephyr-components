/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Protocol callbacks queue commands. Only the UI thread changes the cast session or player.
 * The fetch thread publishes synced bytes under a lock; it never owns the player state.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <dlna/dlna.h>

#include "core/app.h"
#include "core/cfg.h"
#include "core/storage.h"
#include "features/net/net.h"
#include "features/video/player.h"
#include "features/video/ui_player.h"
#include "ui/ui.h"
#include "cast.h"
#include "fetch.h"

enum cast_state { C_NONE, C_PENDING, C_FETCH, C_OPEN, C_FAILED, C_STOPPED };
enum cmd_type { CMD_URI, CMD_PLAY, CMD_PAUSE, CMD_STOP, CMD_SEEK, CMD_FAILED };
enum cast_error { E_NONE, E_LINK, E_CARD, E_ADDRESS, E_SERVER, E_FETCH, E_FORMAT, E_PLAYER, E_RESOLUTION };
struct cmd {
	enum cmd_type type;
	uint32_t arg;
	struct media *media;
};
struct media {
	char uri[DLNA_URI_SIZE], title[128];
};

K_MSGQ_DEFINE(cmds, sizeof(struct cmd), 8, 4);
K_THREAD_STACK_DEFINE(fetch_stack, 4096);
static struct k_thread fetch_thread;
static bool fetch_thread_up;
static atomic_t cstate = ATOMIC_INIT(C_NONE);
static atomic_t last_error;
static atomic_t fetch_cancel;
static struct k_spinlock progress_lock;
static uint64_t fetch_done, fetch_total;
static int fetch_result;
static char cast_url[DLNA_URI_SIZE], cast_title[128], cast_path[96];
static char requested_url[DLNA_URI_SIZE], requested_title[128];
static unsigned int cast_gen;
static bool want_play, first_applied, pending_seek_set, screen_open;
static uint32_t pending_seek;
static bool enabled, started;
static char uuid[37];
static lv_timer_t *timer;
static struct player_growing growing = {
	.max_width = CONFIG_NEKOBOX_DLNA_MAX_WIDTH,
	.max_height = CONFIG_NEKOBOX_DLNA_MAX_HEIGHT,
};

static void remove_cast_files(void)
{
	struct fs_dir_t d;
	struct fs_dirent e;
	char p[384];

	fs_dir_t_init(&d);
	if (fs_opendir(&d, CONFIG_NEKOBOX_DLNA_DIR) != 0) {
		return;
	}
	while (fs_readdir(&d, &e) == 0 && e.name[0]) {
		if (e.type == FS_DIR_ENTRY_FILE) {
			int n = snprintf(p, sizeof(p), "%s/%s", CONFIG_NEKOBOX_DLNA_DIR, e.name);

			if (n > 0 && n < (int)sizeof(p)) {
				fs_unlink(p);
			}
		}
	}
	fs_closedir(&d);
}

static void fetch_progress(void *ctx, uint64_t available, uint64_t total)
{
	k_spinlock_key_t key = k_spin_lock(&progress_lock);

	ARG_UNUSED(ctx);
	fetch_done = available;
	fetch_total = total;
	k_spin_unlock(&progress_lock, key);
}

static uint64_t fetch_avail(void *ctx, int *state)
{
	k_spinlock_key_t key = k_spin_lock(&progress_lock);
	uint64_t available = fetch_done;

	ARG_UNUSED(ctx);
	*state = fetch_result;
	k_spin_unlock(&progress_lock, key);
	if (atomic_get(&fetch_cancel)) {
		*state = -ECANCELED;
	}
	return available;
}

static void fetch_main(void *a, void *b, void *c)
{
	int ret;
	k_spinlock_key_t key;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	ret = dlna_fetch(cast_url, cast_path, &fetch_cancel, fetch_progress, NULL);
	key = k_spin_lock(&progress_lock);
	fetch_result = ret == 0 ? 1 : ret;
	k_spin_unlock(&progress_lock, key);
	printk("dlna: fetch ended with %d\n", ret);
}

/* A cancelled download is reaped by the timer, so DNS/connect waits cannot freeze the UI.
 * Its stack, path and progress stay intact until the thread has actually exited.
 */
static void fetch_reap(void)
{
	if (fetch_thread_up && k_thread_join(&fetch_thread, K_NO_WAIT) == 0) {
		fetch_thread_up = false;
	}
}

static void end_session(enum cast_state to)
{
	atomic_set(&fetch_cancel, 1);
	if (screen_open) {
		ui_goto("dlna");
		screen_open = false;
	}
	atomic_set(&cstate, to);
}

static void fail_session(enum cast_error error, int ret)
{
	printk("dlna: session failed, stage %d (%d)\n", error, ret);
	atomic_set(&last_error, error);
	end_session(C_FAILED);
	ui_goto("dlna");
}

static void start_uri(void)
{
	struct fs_dirent st;
	int ret;

	if (fs_stat(CONFIG_NEKOBOX_DLNA_DIR, &st) != 0) {
		ret = fs_mkdir(CONFIG_NEKOBOX_DLNA_DIR);
		if (ret != 0) {
			fail_session(E_CARD, ret);
			return;
		}
	}
	remove_cast_files();
	ret = snprintf(cast_path, sizeof(cast_path), "%s/cast%u.mp4",
		       CONFIG_NEKOBOX_DLNA_DIR, ++cast_gen);
	if (ret < 0 || ret >= (int)sizeof(cast_path)) {
		fail_session(E_CARD, -ENAMETOOLONG);
		return;
	}
	strcpy(cast_url, requested_url);
	strcpy(cast_title, requested_title);
	fetch_done = fetch_total = 0;
	fetch_result = 0;
	atomic_clear(&fetch_cancel);
	atomic_set(&cstate, C_FETCH);
	k_thread_create(&fetch_thread, fetch_stack, K_THREAD_STACK_SIZEOF(fetch_stack), fetch_main,
			NULL, NULL, NULL, 11, 0, K_NO_WAIT);
	k_thread_name_set(&fetch_thread, "dlna_fetch");
	fetch_thread_up = true;
	printk("dlna: receiving video into %s\n", cast_path);
}

static int reject_uri(enum cast_error error, int ret)
{
	struct cmd c = {.type = CMD_FAILED, .arg = error};

	printk("dlna: media URI rejected (%d)\n", ret);
	k_msgq_put(&cmds, &c, K_NO_WAIT);
	return ret;
}

static int op_set_uri(void *ctx, const char *uri, const char *title)
{
	struct cmd c = {.type = CMD_URI};
	int ret;

	ARG_UNUSED(ctx);
	if (strncmp(uri, "http://", 7)) {
		return reject_uri(E_LINK, -ENOTSUP);
	}
	if (!storage_ready()) {
		return reject_uri(E_CARD, -ENODEV);
	}
	if (strlen(uri) >= DLNA_URI_SIZE) {
		return reject_uri(E_ADDRESS, -ENAMETOOLONG);
	}
	c.media = malloc(sizeof(*c.media));
	if (!c.media) {
		return -ENOMEM;
	}
	strcpy(c.media->uri, uri);
	snprintf(c.media->title, sizeof(c.media->title), "%s", title ? title : "");
	ret = k_msgq_put(&cmds, &c, K_NO_WAIT);
	if (ret) {
		free(c.media);
	}
	printk("dlna: media address %s (%u bytes)\n", ret ? "busy" : "accepted",
	       (unsigned)strlen(uri));
	return ret ? -EBUSY : 0;
}

static int op_check_resolution(void *ctx, uint32_t width, uint32_t height)
{
	ARG_UNUSED(ctx);
	if (player_growing_check_resolution(&growing, width, height) != 0) {
		return reject_uri(E_RESOLUTION, -ENOTSUP);
	}
	return 0;
}

static int post(enum cmd_type t, uint32_t arg)
{
	struct cmd c = {.type = t, .arg = arg};

	return k_msgq_put(&cmds, &c, K_NO_WAIT) == 0 ? 0 : -EBUSY;
}

static int op_play(void *ctx) { ARG_UNUSED(ctx); return post(CMD_PLAY, 0); }
static int op_pause(void *ctx) { ARG_UNUSED(ctx); return post(CMD_PAUSE, 0); }
static int op_stop(void *ctx) { ARG_UNUSED(ctx); return post(CMD_STOP, 0); }
static int op_seek(void *ctx, uint32_t ms) { ARG_UNUSED(ctx); return post(CMD_SEEK, ms); }

static enum dlna_state op_state(void *ctx)
{
	ARG_UNUSED(ctx);
	switch (atomic_get(&cstate)) {
	case C_NONE: return DLNA_NO_MEDIA;
	case C_PENDING:
	case C_FETCH: return DLNA_TRANSITIONING;
	case C_FAILED: return DLNA_ERROR;
	case C_STOPPED: return DLNA_STOPPED;
	case C_OPEN:
		switch (player_state()) {
		case PLAYER_PLAYING: return DLNA_PLAYING;
		case PLAYER_PAUSED: return DLNA_PAUSED;
		case PLAYER_ENDED: return DLNA_STOPPED;
		case PLAYER_ERROR: return DLNA_ERROR;
		default: return DLNA_TRANSITIONING;
		}
	}
	return DLNA_STOPPED;
}

static uint32_t op_position(void *ctx)
{
	ARG_UNUSED(ctx);
	return atomic_get(&cstate) == C_OPEN ? player_position_ms() : 0;
}

static uint32_t op_duration(void *ctx)
{
	ARG_UNUSED(ctx);
	return atomic_get(&cstate) == C_OPEN ? ui_player_duration_ms() : 0;
}

static int op_get_volume(void *ctx) { ARG_UNUSED(ctx); return player_volume(); }
static int op_set_volume(void *ctx, int percent)
{
	ARG_UNUSED(ctx);
	player_set_volume(percent);
	return 0;
}

static const struct dlna_ops ops = {
	.set_uri = op_set_uri, .play = op_play, .pause = op_pause, .stop = op_stop,
	.check_resolution = op_check_resolution,
	.seek = op_seek, .state = op_state, .position_ms = op_position, .duration_ms = op_duration,
	.get_volume = op_get_volume, .set_volume = op_set_volume,
};
static struct dlna_config config = {.ops = &ops};

static void apply_command(const struct cmd *c)
{
	switch (c->type) {
	case CMD_URI:
		atomic_clear(&last_error);
		end_session(C_PENDING);
		strcpy(requested_url, c->media->uri);
		strcpy(requested_title, c->media->title);
		free(c->media);
		want_play = first_applied = pending_seek_set = false;
		ui_goto("dlna");
		break;
	case CMD_FAILED:
		atomic_set(&last_error, c->arg);
		end_session(C_FAILED);
		ui_goto("dlna");
		break;
	case CMD_PLAY:
		want_play = true;
		if (atomic_get(&cstate) == C_OPEN) {
			player_play();
		} else if (atomic_get(&cstate) == C_STOPPED && requested_url[0]) {
			first_applied = pending_seek_set = false;
			atomic_set(&cstate, C_PENDING);
		}
		break;
	case CMD_PAUSE:
		want_play = false;
		if (atomic_get(&cstate) == C_OPEN) {
			player_pause();
		}
		break;
	case CMD_SEEK:
		if (atomic_get(&cstate) == C_OPEN) {
			player_seek(c->arg);
		} else {
			pending_seek = c->arg;
			pending_seek_set = true;
		}
		break;
	case CMD_STOP:
		if (atomic_get(&cstate) != C_NONE) {
			end_session(C_STOPPED);
		}
		break;
	}
}

static void cast_tick(lv_timer_t *t)
{
	struct cmd c;
	int state, ret;
	uint64_t available;
	enum player_state ps;

	ARG_UNUSED(t);
	while (k_msgq_get(&cmds, &c, K_NO_WAIT) == 0) {
		apply_command(&c);
	}
	fetch_reap();
	if (atomic_get(&cstate) == C_PENDING && !fetch_thread_up) {
		start_uri();
	}
	if (atomic_get(&cstate) == C_FETCH) {
		available = fetch_avail(NULL, &state);
		if (state < 0) {
			fail_session(E_FETCH, state);
			return;
		}
		if (state != 1 && available < CONFIG_NEKOBOX_DLNA_BUFFER_KB * 1024U) {
			return;
		}
		{
			k_spinlock_key_t key = k_spin_lock(&progress_lock);

			growing.total = fetch_total;
			k_spin_unlock(&progress_lock, key);
		}
		growing.avail = fetch_avail;
		ret = player_growing_ready(cast_path, &growing);
		if (ret == -EAGAIN) {
			return;
		}
		if (ret != 0) {
			fail_session(ret == -E2BIG ? E_RESOLUTION : E_FORMAT, ret);
			return;
		}
		screen_open = true;
		ret = ui_player_open_growing(cast_path, &growing, "dlna");
		if (ret != 0) {
			fail_session(ret == -E2BIG ? E_RESOLUTION : E_PLAYER, ret);
			return;
		}
		atomic_set(&cstate, C_OPEN);
		first_applied = false;
	}
	if (atomic_get(&cstate) != C_OPEN) {
		return;
	}
	ps = player_state();
	if (ps == PLAYER_IDLE) {
		/* The back button has already closed the player. */
		screen_open = false;
		end_session(C_STOPPED);
		return;
	}
	fetch_avail(NULL, &state);
	if (state < 0 || ps == PLAYER_ERROR) {
		fail_session(state < 0 ? E_FETCH : E_PLAYER, state);
		return;
	}
	if (!first_applied && (ps == PLAYER_PLAYING || ps == PLAYER_PAUSED)) {
		first_applied = true;
		if (pending_seek_set) {
			player_seek(pending_seek);
			pending_seek_set = false;
		}
		if (want_play) {
			player_play();
		} else {
			player_pause();
		}
	}
}

void cast_enable(bool on)
{
	if (on == enabled) {
		return;
	}
	enabled = on;
	printk("dlna: receiver %s\n", on ? "enabled" : "disabled");
	if (on) {
		app_net_keep_awake(true);
		if (!started) {
			int ret;

			config.name = CONFIG_NEKOBOX_DLNA_NAME;
			config.uuid = uuid;
			ret = dlna_start(&config);
			started = ret == 0;
			if (!started) {
				atomic_set(&last_error, E_SERVER);
				atomic_set(&cstate, C_FAILED);
				printk("dlna: receiver could not start (%d)\n", ret);
			}
		}
	} else {
		struct cmd c;

		if (started) {
			dlna_stop();
			started = false;
		}
		while (k_msgq_get(&cmds, &c, K_NO_WAIT) == 0) {
			free(c.media);
		}
		end_session(C_NONE);
		app_net_keep_awake(false);
	}
}

bool cast_enabled(void) { return enabled; }

void cast_status(char *buf, size_t size)
{
	if (!enabled) {
		snprintf(buf, size, "Off");
	} else if (!app_net_connected()) {
		snprintf(buf, size, "On, waiting for the network");
	} else if (atomic_get(&cstate) == C_FETCH || atomic_get(&cstate) == C_PENDING) {
		k_spinlock_key_t key = k_spin_lock(&progress_lock);
		uint64_t done = fetch_done, total = fetch_total;

		k_spin_unlock(&progress_lock, key);
		if (total) {
			snprintf(buf, size, "Loading %llu of %llu KiB", (unsigned long long)(done >> 10),
				 (unsigned long long)(total >> 10));
		} else {
			snprintf(buf, size, "Loading %llu KiB", (unsigned long long)(done >> 10));
		}
	} else if (atomic_get(&cstate) == C_OPEN) {
		snprintf(buf, size, "Casting %s", cast_title[0] ? cast_title : "video");
	} else if (atomic_get(&cstate) == C_FAILED) {
		if (atomic_get(&last_error) == E_RESOLUTION) {
			snprintf(buf, size, "Video exceeds %ux%u. Select %up or lower in your casting app.",
				 CONFIG_NEKOBOX_DLNA_MAX_WIDTH, CONFIG_NEKOBOX_DLNA_MAX_HEIGHT,
				 MIN(CONFIG_NEKOBOX_DLNA_MAX_WIDTH, CONFIG_NEKOBOX_DLNA_MAX_HEIGHT));
			return;
		}
		static const char *const errors[] = {
			[E_NONE] = "The video could not be loaded",
			[E_LINK] = "Unsupported link. Use an HTTP MP4 video.",
			[E_CARD] = "Cannot write to the SD card.",
			[E_ADDRESS] = "The video address is too long.",
			[E_SERVER] = "Receiver failed to start. Stop and try again.",
			[E_FETCH] = "Download failed. Check the source and Wi-Fi.",
			[E_FORMAT] = "Unsupported video. Use an H.264/AAC MP4 file.",
			[E_PLAYER] = "The video could not be played.",
		};

		snprintf(buf, size, "%s", errors[atomic_get(&last_error)]);
	} else {
		snprintf(buf, size, "On, shown as %s", CONFIG_NEKOBOX_DLNA_NAME);
	}
}

static int cast_step(void)
{
	uint8_t id[16];
	const char *v;
	ssize_t n = hwinfo_get_device_id(id, sizeof(id));

	dlna_uuid_from_id(id, n > 0 ? n : 0, uuid);
	timer = lv_timer_create(cast_tick, 100, NULL);
	remove_cast_files();
	v = cfg_get("dlna");
	printk("dlna: saved %s, connections %d, contexts %d, descriptors %d\n",
	       v && v[0] == '1' ? "on" : "off", CONFIG_NET_MAX_CONN, CONFIG_NET_MAX_CONTEXTS,
	       CONFIG_ZVFS_OPEN_MAX);
	printk("dlna: maximum cast resolution %ux%u\n", CONFIG_NEKOBOX_DLNA_MAX_WIDTH,
	       CONFIG_NEKOBOX_DLNA_MAX_HEIGHT);
	if (v && v[0] == '1') {
		cast_enable(true);
	}
	return 0;
}
NEKOBOX_INIT(NEKOBOX_STAGE_SERVICES, 20, "dlna", cast_step);

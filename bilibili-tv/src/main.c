/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Bilibili client: lists of videos (recommended, popular, search results) with covers, a tap
 * downloads the 360P file of the video to the card and plays it with the player of NekoBox.
 *
 * Threads: the main thread owns LVGL; the worker does everything that waits for the network or the
 * card and drives the player; the shell and the touch panel only post commands.
 */

#include <ff.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app.h"
#include "cover.h"
#include "bili_api.h"
#include "download.h"
#include "http.h"
#include "features/video/player.h"
#include "wifi.h"

struct cmd {
	enum app_cmd_type type;
	int arg;
	char ssid[33], psk[65];	/* the keyword of a search shares psk and ssid: see app_cmd_search() */
	char keyword[96];
};

#define PAGE_SIZE	20

/* a list as the user interface shows it; the worker changes it, the UI thread reads it */
struct vlist {
	struct bili_video v[MAX_VIDEOS];
	int count;
	int next_page;
	int pages;		/* of a search, 0 when unknown */
	bool more;
	bool loading;
	int error;		/* what the last page request returned, 0 when it worked */
	uint32_t gen;
	char keyword[96];
};

K_MSGQ_DEFINE(cmd_q, sizeof(struct cmd), 8, 4);
static atomic_t stop_req;

static K_MUTEX_DEFINE(lists_lock);
static struct vlist lists[LIST_COUNT];

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

static K_THREAD_STACK_DEFINE(worker_stack, 12288);
static struct k_thread worker_thread;

void app_cmd(enum app_cmd_type type, int arg)
{
	struct cmd c = {.type = type, .arg = arg};

	if (type == CMD_STOP) {
		/* the flag is the whole command: a message left in the queue would stop the next video as soon as it opens */
		atomic_set(&stop_req, 1);
		return;
	}
	if (k_msgq_put(&cmd_q, &c, K_NO_WAIT) != 0) {
		printk("app: command queue full\n");
	}
}

void app_cmd_wifi(const char *ssid, const char *psk)
{
	struct cmd c = {.type = CMD_WIFI};

	snprintf(c.ssid, sizeof(c.ssid), "%s", ssid);
	snprintf(c.psk, sizeof(c.psk), "%s", psk);
	k_msgq_put(&cmd_q, &c, K_NO_WAIT);
}

/* copies a keyword; a cut in the middle of a character (3 bytes of a Chinese one) would make the request invalid */
static void keyword_copy(char *dst, size_t size, const char *src)
{
	size_t n = strnlen(src, size - 1);

	/* back to the start of a character that did not fit completely */
	if (src[n] != '\0') {
		while (n > 0 && ((uint8_t)src[n] & 0xc0) == 0x80) {
			n--;
		}
	}
	memcpy(dst, src, n);
	while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\t')) {
		n--;
	}
	dst[n] = '\0';
}

void app_cmd_search(const char *keyword)
{
	struct cmd c = {.type = CMD_SEARCH};

	keyword_copy(c.keyword, sizeof(c.keyword), keyword);
	if (k_msgq_put(&cmd_q, &c, K_NO_WAIT) != 0) {
		printk("app: command queue full\n");
	}
}

void app_list_info(enum list_id list, struct list_info *info)
{
	k_mutex_lock(&lists_lock, K_FOREVER);
	info->count = lists[list].count;
	info->gen = lists[list].gen;
	info->more = lists[list].more;
	info->loading = lists[list].loading;
	info->error = lists[list].error;
	k_mutex_unlock(&lists_lock);
}

bool app_video(enum list_id list, int index, struct bili_video *out)
{
	bool ok;

	k_mutex_lock(&lists_lock, K_FOREVER);
	ok = list < LIST_COUNT && index >= 0 && index < lists[list].count;
	if (ok) {
		*out = lists[list].v[index];
	}
	k_mutex_unlock(&lists_lock);

	return ok;
}

void app_mem_probe(unsigned int *free_kib, unsigned int *biggest_kib)
{
	enum { CHUNK = 64 * 1024, MAXCH = 160 };
	void **blocks = calloc(MAXCH, sizeof(void *));
	int n = 0;
	size_t lo = 0, hi;

	*free_kib = *biggest_kib = 0;
	if (blocks == NULL) {
		return;
	}
	while (n < MAXCH && (blocks[n] = malloc(CHUNK)) != NULL) {
		n++;
	}
	for (int i = 0; i < n; i++) {
		free(blocks[i]);
	}
	/* the biggest block: bisection between what failed and what worked */
	hi = (size_t)(n + 1) * CHUNK;
	while (hi - lo > 4096) {
		size_t mid = (lo + hi) / 2;
		void *q = malloc(mid);

		if (q != NULL) {
			free(q);
			lo = mid;
		} else {
			hi = mid;
		}
	}
	free(blocks);
	*free_kib = n * 64;
	*biggest_kib = lo >> 10;
}

static void mem_note(const char *where)
{
	unsigned int f, b;

	app_mem_probe(&f, &b);
	printk("mem %s: %u KiB free, biggest block %u KiB\n", where, f, b);
}

/* ---- lists --------------------------------------------------------------------------------- */

static void list_loading(enum list_id list, bool on)
{
	k_mutex_lock(&lists_lock, K_FOREVER);
	lists[list].loading = on;
	k_mutex_unlock(&lists_lock);
}

/* the first page replaces the list, the others are appended without the videos already there */
static void list_store(enum list_id list, int page, const struct bili_video *v, int n, int pages)
{
	struct vlist *l = &lists[list];

	k_mutex_lock(&lists_lock, K_FOREVER);
	if (page == 1) {
		l->count = 0;
		l->gen++;
	}
	for (int i = 0; i < n && l->count < MAX_VIDEOS; i++) {
		bool dup = false;

		for (int j = 0; j < l->count && !dup; j++) {
			dup = strcmp(l->v[j].bvid, v[i].bvid) == 0;
		}
		if (!dup) {
			l->v[l->count++] = v[i];
		}
	}
	l->next_page = page + 1;
	l->pages = pages;
	l->more = n >= PAGE_SIZE / 2 && l->count < MAX_VIDEOS && (pages == 0 || page < pages);
	l->loading = false;
	k_mutex_unlock(&lists_lock);
}

static const char *list_name(enum list_id list)
{
	return list == LIST_RECOMMEND ? "recommend" : (list == LIST_POPULAR ? "popular" : "search");
}

/* fetches a page of a list; page 1 starts it again */
static void load(enum list_id list, int page)
{
	struct bili_video *v = malloc(sizeof(*v) * PAGE_SIZE);
	char kw[96];
	int n, pages = 0;

	if (v == NULL) {
		return;
	}
	k_mutex_lock(&lists_lock, K_FOREVER);
	snprintf(kw, sizeof(kw), "%s", lists[list].keyword);
	k_mutex_unlock(&lists_lock);
	if (list == LIST_SEARCH && kw[0] == '\0') {
		free(v);
		return;
	}
	list_loading(list, true);
	if (page == 1) {
		ui_set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe5\x8a\xa0\xe8\xbd\xbd...");	/* loading */
	}
	switch (list) {
	case LIST_RECOMMEND:
		n = bili_api_feed(page, v, PAGE_SIZE);
		break;
	case LIST_POPULAR:
		n = bili_api_popular(page, v, PAGE_SIZE);
		break;
	default:
		n = bili_api_search(kw, page, v, PAGE_SIZE, &pages);
		break;
	}
	k_mutex_lock(&lists_lock, K_FOREVER);
	lists[list].error = n < 0 ? n : 0;
	k_mutex_unlock(&lists_lock);
	if (n < 0) {
		list_loading(list, false);
		ui_set_status("\xe5\x8a\xa0\xe8\xbd\xbd\xe5\xa4\xb1\xe8\xb4\xa5 (%d)", n);	/* failed */
		printk("list %s page %d failed (%d)\n", list_name(list), page, n);
	} else {
		list_store(list, page, v, n, pages);
		ui_set_status("");
		printk("list %s page %d: %d videos (%d pages)\n", list_name(list), page, n, pages);
	}
	free(v);
}

static volatile bool loading_text;	/* the download is the only thing to wait for: say so */

static int on_progress(uint64_t done, uint64_t total, void *ctx)
{
	if (total > 0) {
		ui_set_buffered((int)(done * 1000 / total));
		if (loading_text) {
			ui_set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe7\xbc\x93\xe5\x86\xb2 %u%%", (unsigned int)(done * 100 / total));	/* buffering */
		}
	}

	return atomic_get(&stop_req) ? -ECANCELED : 0;
}

/* the cached file is good when it has the size the server announced */
static bool cached(const char *path, uint64_t size)
{
	struct fs_dirent st;

	return size > 0 && fs_stat(path, &st) == 0 && st.type == FS_DIR_ENTRY_FILE && st.size == size;
}

/* plays until the end or a stop; returns when the player is closed */
static void play_loop(const struct bili_video *v, const struct player_info *info, bool growing)
{
	uint32_t last_pos = UINT32_MAX;
	int64_t moved = k_uptime_get();
	bool waiting = false;

	ui_set_playing(v->title, info->duration_ms);
	ui_set_status("");
	while (!atomic_get(&stop_req)) {
		enum player_state st = player_state();
		uint32_t pos = player_position_ms();
		struct cmd c;

		if (st == PLAYER_ERROR) {
			printk("play: the player stopped with an error (decoder or file), position %u ms\n", pos);
			ui_set_status("\xe6\x92\xad\xe6\x94\xbe\xe5\x87\xba\xe9\x94\x99 (\xe8\xa7\xa3\xe7\xa0\x81\xe5\x99\xa8)");	/* playback error (decoder) */
			k_msleep(2500);
			break;
		}
		if (st == PLAYER_ENDED || st == PLAYER_IDLE) {
			break;
		}
		if (pos != last_pos || st != PLAYER_PLAYING) {
			last_pos = pos;
			moved = k_uptime_get();
			if (waiting) {
				waiting = false;
				ui_set_status("");
			}
		} else if (growing && !waiting && k_uptime_get() - moved > 1500) {
			/* the picture stands: the download is behind */
			waiting = true;
			ui_set_status("\xe7\xbc\x93\xe5\x86\xb2\xe4\xb8\xad...");		/* buffering */
		}
		ui_set_position(pos, st == PLAYER_PAUSED);
		if (k_msgq_get(&cmd_q, &c, K_MSEC(200)) == 0) {
			if (c.type == CMD_PAUSE) {
				player_state() == PLAYER_PAUSED ? player_play() : player_pause();
			} else if (c.type == CMD_STOP) {
				break;
			}
		}
	}
	printk("play: stopping\n");
	if (growing) {
		/* the readers wait for data: let them fail before the player is closed */
		dl_stop();
		printk("play: the download is stopped\n");
	}
	player_close();
	printk("play: the player is closed\n");
	ui_set_status("");
	ui_set_playing(NULL, 0);
}

static void play(enum list_id list, int index)
{
	struct bili_video v;
	struct bili_stream s;
	struct player_info info;
	struct fs_dirent st;
	char path[96];
	int ret;
	bool growing = false;

	if (!app_video(list, index, &v)) {
		ui_set_status("no video %d in list %d", index, list);
		return;
	}
	atomic_set(&stop_req, 0);

	/*
	 * The player page first: the lists let go of their covers there, and the covers are thrown out
	 * so that the decoder finds its pictures in the memory they held.
	 */
	ui_set_playing(v.title, 0);
	for (int i = 0; i < 50 && cover_in_use() > 0; i++) {
		k_msleep(20);
	}
	cover_flush();
	mem_note("after the covers are gone");

	ui_set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe8\x8e\xb7\xe5\x8f\x96\xe6\x92\xad\xe6\x94\xbe\xe5\x9c\xb0\xe5\x9d\x80...");	/* getting the address */
	ret = bili_api_stream(&v, &s);
	if (ret != 0) {
		ui_set_status("\xe6\x97\xa0\xe6\xb3\x95\xe8\x8e\xb7\xe5\x8f\x96\xe6\x92\xad\xe6\x94\xbe\xe5\x9c\xb0\xe5\x9d\x80 (%d)", ret);	/* no stream address */
		goto leave;
	}
	printk("play: %s quality %d, %llu bytes, %u ms\n  %s\n", v.bvid, s.quality,
	       (unsigned long long)s.size, s.length_ms, s.url);

	/* the comments: nothing to wait for when they cannot be had */
	{
		struct dm_set *dm = IS_ENABLED(CONFIG_BILI_DM_FETCH) ? malloc(sizeof(*dm)) : NULL;
		int n = -ENOMEM;

		if (dm != NULL) {
			ui_set_status("\xe5\xbc\xb9\xe5\xb9\x95...");		/* danmaku */
			dm_init(dm, CONFIG_BILI_DM_MAX_ITEMS, CONFIG_BILI_DM_POOL_KB * 1024U);
			n = bili_api_danmaku(s.cid, dm);
			if (n <= 0) {
				dm_free(dm);
				free(dm);
				dm = NULL;
			}
		}
		printk("danmaku: %d comments\n", n);
		ui_set_danmaku(dm);
	}

	if (fs_stat(CONFIG_BILI_CACHE_DIR, &st) != 0) {
		fs_mkdir(CONFIG_BILI_CACHE_DIR);
	}
	snprintf(path, sizeof(path), CONFIG_BILI_CACHE_DIR "/%s.mp4", v.bvid);
	ui_set_buffered(0);
	if (!IS_ENABLED(CONFIG_BILI_STREAM) && !cached(path, s.size)) {
		/* the whole file first */
		loading_text = true;
		ui_set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe7\xbc\x93\xe5\x86\xb2 0%%");		/* buffering */
		ret = dl_file(s.url, path, s.size, on_progress, NULL);
		loading_text = false;
		if (ret == -ECANCELED) {
			ui_set_status("");
			ui_set_playing(NULL, 0);
			return;
		}
		if (ret != 0) {
			printk("play: the download failed (%d), http %d\n", ret, dl_last_status());
			ui_set_status("\xe4\xb8\x8b\xe8\xbd\xbd\xe5\xa4\xb1\xe8\xb4\xa5 (%d)", ret);	/* download failed */
			goto leave;
		}
	}
	if (cached(path, s.size)) {
		ui_set_buffered(1000);
		mem_note("before the player");
		ret = player_open(path, &info);
		mem_note("with the player open");
	} else {
		/* the file is played while it comes: the header is at its beginning */
		struct player_growing g = {.total = s.size, .avail = dl_avail};

		loading_text = true;
		ui_set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe7\xbc\x93\xe5\x86\xb2 0%%");		/* buffering */
		ret = s.size > 0 ? dl_start(s.url, path, s.size, on_progress, NULL) : -EINVAL;
		if (ret != 0) {
			ui_set_status("\xe4\xb8\x8b\xe8\xbd\xbd\xe5\xa4\xb1\xe8\xb4\xa5 (%d)", ret);	/* download failed */
			goto leave;
		}
		mem_note("before the player");
		ret = player_open_growing(path, &g, &info);
		mem_note("with the player open");
		printk("play: player_open_growing returned %d\n", ret);
		loading_text = false;
		growing = true;
		if (ret != 0) {
			dl_stop();
			fs_unlink(path);
		}
	}
	if (ret != 0) {
		if (atomic_get(&stop_req)) {
			ui_set_status("");
			ui_set_playing(NULL, 0);
			return;
		}
		if (growing && dl_last_status() >= 400) {
			printk("play: the download failed with HTTP %d (open returned %d)\n", dl_last_status(), ret);
			ui_set_status("\xe4\xb8\x8b\xe8\xbd\xbd\xe5\xa4\xb1\xe8\xb4\xa5 (HTTP %d)", dl_last_status());	/* download failed */
		} else if (ret == -ENOMEM) {
			printk("play: out of memory while opening the video (%d)\n", ret);
			ui_set_status("\xe5\x86\x85\xe5\xad\x98\xe4\xb8\x8d\xe8\xb6\xb3 (%d)", ret);			/* out of memory */
		} else {
			printk("play: cannot open the video: %d (%s), download http %d\n", ret,
			       growing ? "growing file" : "cached file", growing ? dl_last_status() : 0);
			ui_set_status("\xe6\x97\xa0\xe6\xb3\x95\xe6\x92\xad\xe6\x94\xbe (%d)", ret);	/* cannot play */
		}
		goto leave;
	}
	player_play();
	play_loop(&v, &info, growing);
	if (growing) {
		int state;

		dl_avail(NULL, &state);
		if (state != 1) {
			fs_unlink(path);	/* a partial file is not kept */
		}
	}
	return;

leave:
	/* the reason stays on the screen for a moment, then the list is back */
	k_msleep(2500);
	ui_set_playing(NULL, 0);
}

static void wifi_connect(const char *ssid, const char *psk)
{
	int ret;

	ui_set_status("Wi-Fi: %s ...", ssid);
	ret = wifi_join(ssid, psk, 30);
	if (ret != 0) {
		ui_set_status("Wi-Fi %s: error %d", ssid, ret);
		printk("wifi: join %s failed (%d)\n", ssid, ret);
		return;
	}
	ui_set_status("Wi-Fi connected");
	load(LIST_RECOMMEND, 1);
}

/* /SD:/wifi.cfg: the name on the first line, the password on the second (the file of NekoBox) */
static void wifi_cfg_read(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
	struct fs_file_t f;
	char buf[128];
	ssize_t n;
	char *nl;

	fs_file_t_init(&f);
	if (fs_open(&f, "/SD:/wifi.cfg", FS_O_READ) != 0) {
		return;
	}
	n = fs_read(&f, buf, sizeof(buf) - 1);
	fs_close(&f);
	if (n <= 0) {
		return;
	}
	buf[n] = '\0';
	nl = strchr(buf, '\n');
	if (nl != NULL) {
		*nl++ = '\0';
	}
	buf[strcspn(buf, "\r")] = '\0';
	snprintf(ssid, ssid_size, "%s", buf);
	if (nl != NULL) {
		nl[strcspn(nl, "\r\n")] = '\0';
		snprintf(psk, psk_size, "%s", nl);
	}
	printk("wifi: network %s from the card\n", ssid);
}

void bili_selftest_start(void);
static void card_wait(void);

static void worker_main(void *a, void *b, void *c)
{
	struct cmd cmd;

	char ssid[33] = CONFIG_BILI_WIFI_SSID, psk[65] = CONFIG_BILI_WIFI_PSK;

	card_wait();
	if (ssid[0] == '\0') {
		wifi_cfg_read(ssid, sizeof(ssid), psk, sizeof(psk));
	}
	if (ssid[0] != '\0') {
		wifi_connect(ssid, psk);
	} else {
		ui_set_status("No Wi-Fi: put /SD:/wifi.cfg on the card or type  bili wifi <ssid> <psk>");
	}
	for (;;) {
		k_msgq_get(&cmd_q, &cmd, K_FOREVER);
		switch (cmd.type) {
		case CMD_REFRESH:
			load(cmd.arg, 1);
			break;
		case CMD_MORE: {
			int page;

			k_mutex_lock(&lists_lock, K_FOREVER);
			page = lists[cmd.arg].next_page;
			k_mutex_unlock(&lists_lock);
			if (page > 1) {
				load(cmd.arg, page);
			}
			break;
		}
		case CMD_SEARCH:
			k_mutex_lock(&lists_lock, K_FOREVER);
			keyword_copy(lists[LIST_SEARCH].keyword, sizeof(lists[LIST_SEARCH].keyword), cmd.keyword);
			k_mutex_unlock(&lists_lock);
			load(LIST_SEARCH, 1);
			break;
		case CMD_HOT: {
			static char words[10][BILI_HOT_WORD_LEN];
			int n = bili_api_hot_words(words, ARRAY_SIZE(words));

			if (n > 0) {
				ui_set_hot_words(words, n);
			}
			printk("hot words: %d\n", n);
			for (int i = 0; i < n; i++) {
				printk("  %s\n", words[i]);
			}
			break;
		}
		case CMD_PLAY:
			play(cmd.arg >> 16, cmd.arg & 0xffff);
			break;
		case CMD_WIFI:
			if (cmd.ssid[0] == '\0') {
				wifi_cfg_read(cmd.ssid, sizeof(cmd.ssid), cmd.psk, sizeof(cmd.psk));
			}
			if (cmd.ssid[0] != '\0') {
				wifi_connect(cmd.ssid, cmd.psk);
			}
			break;
		default:
			break;
		}
	}
}

/* the card does not always answer at the first try (a slot without card detect): ask again until it does */
/*
 * Starts the card from the beginning (CMD0 and the whole init) and reads the first sector: a card
 * that came up wrong answers every read with 0xff until it is initialised again. True when the
 * sector ends with the 0x55 0xaa of a boot sector.
 */
static bool card_reinit(int tries)
{
	uint8_t sec[512] __aligned(64);

	disk_access_ioctl("SD", DISK_IOCTL_CTRL_DEINIT, NULL);
	if (disk_access_init("SD") != 0 || disk_access_read("SD", sec, 0, 1) != 0) {
		printk("app: card try %d: init or read of sector 0 failed\n", tries);
		return false;
	}
	printk("app: card try %d: sector 0 %02x %02x %02x ... %02x %02x\n", tries, sec[0], sec[1], sec[2],
	       sec[510], sec[511]);

	return sec[510] == 0x55 && sec[511] == 0xaa;
}

/* the card does not always answer at the first try (a slot without card detect): ask again until it does */
static void card_wait(void)
{
	for (int tries = 1;; tries++) {
		int ret = fs_mount(&mp);

		if (ret == 0 || ret == -EBUSY) {
			if (tries > 1) {
				printk("app: card mounted after %d tries\n", tries);
				ui_set_status("");
			}
			return;
		}
		printk("app: no card (%d), starting it again\n", ret);
		ui_set_status("\xe6\xad\xa3\xe5\x9c\xa8\xe7\xad\x89\xe5\xbe\x85\xe5\xad\x98\xe5\x82\xa8\xe5\x8d\xa1 (%d)...", ret);	/* waiting for the card */
		/* start it again until the first sector is a boot sector, then mount */
		for (int i = 0; i < 5 && !card_reinit(tries * 10 + i); i++) {
			k_msleep(300);
		}
		k_msleep(500);
	}
}

int main(void)
{
	http_set_proxy(CONFIG_BILI_PROXY);
	ui_init();
#ifdef CONFIG_BILI_SELFTEST
	bili_selftest_start();
#endif
	k_thread_create(&worker_thread, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack), worker_main,
			NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	k_thread_name_set(&worker_thread, "worker");
	for (;;) {
		static int64_t beat;

		k_msleep(ui_run());
		if (IS_ENABLED(CONFIG_BILI_HEARTBEAT) && k_uptime_get() - beat >= 10000) {
			char s[200];

			beat = k_uptime_get();
			cover_stats(s, sizeof(s));
			printk("alive %u s; %s", (unsigned int)(beat / 1000), s);
			if (player_state() != PLAYER_IDLE) {
				printk("; player state %d at %u ms", (int)player_state(), player_position_ms());
			}
			printk("\n");
		}
	}

	return 0;
}

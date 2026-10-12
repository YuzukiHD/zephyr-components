/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The game screen: a Game Boy, Game Boy Color or Game Boy Advance (mGBA core) or a NES (nofrendo
 * core) from the SD card. The emulator thread runs the console a frame at a time and is paced by
 * the sound: the codec takes 48000 stereo frames a second, a full ring holds the emulation back.
 * The picture goes to the scaling plane of the display engine, the keys are translucent buttons
 * of LVGL over it; the fingers are read straight from the touch events (one per slot), because
 * the pointer of LVGL handles one finger and a game needs the directions and a button together.
 *
 * A ROM that does not fit in the memory is refused with a message instead of started.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/audio/codec.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/ring_buffer.h>

#include <mgba_zephyr/gba.h>
#include <nes_zephyr/nesemu.h>

#ifdef CONFIG_DEMAND_PAGING_STATS
#include <zephyr/kernel/mm/demand_paging.h>
#endif
#include "core/app.h"
#include "debug/profile.h"
#include "features/games/game.h"
#include "ui/ui.h"

#define RATE		48000
#define HIGH_WATER	(RATE / 15)
#define SAVE_MS		3000
/* below SKIP_BELOW queued sound frames the emulation draws less, until the queue is SKIP_RESUME */
#define SKIP_BELOW	1500
#define SKIP_RESUME	2800

#define BLOCK_FRAMES	512
#define BLOCK_BYTES	(BLOCK_FRAMES * 2 * sizeof(int16_t))
#define SLAB_BLOCKS	6
#define PREFILL		3
#define RING_BLOCKS	8

/* below the user interface thread (the main thread, 8), which must always get its turn */
#define EMU_PRIO	9

#define NBUF		4
#define BUF_STRIDE	(256 * 2)
#define BUF_SIZE	ROUND_UP(BUF_STRIDE * 240, 64)

enum core { CORE_NONE, CORE_GBA, CORE_NES };

/* the keys of the pad, whatever the console: translated for the core when the frame starts */
enum {
	K_A, K_B, K_SELECT, K_START, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_L, K_R, K_COUNT,
};

static const struct device *const i2s = DEVICE_DT_GET(DT_NODELABEL(audio_codec));
static const struct device *const ctl = DEVICE_DT_GET(DT_NODELABEL(codec_analog));
static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

K_MEM_SLAB_DEFINE_STATIC(slab, BLOCK_BYTES, SLAB_BLOCKS, 4);
RING_BUF_DECLARE(ring, RING_BLOCKS * BLOCK_BYTES);
K_SEM_DEFINE(frame_sem, 0, 1);
static K_THREAD_STACK_DEFINE(game_emu_stack, 16384);
static K_THREAD_STACK_DEFINE(game_audio_stack, 4096);
static K_THREAD_STACK_DEFINE(game_video_stack, 8192);
static struct k_thread emu_thread, game_audio_thread, game_video_thread;

static volatile bool quit, running;
static bool threads_started;
static volatile int status;		/* 0: loading, 1: playing, <0: the error that stopped it */
static enum core core;
static char rom_path[192], save_path[200];
static uint8_t *buf[NBUF];
static unsigned int cur;
static atomic_t ready_idx;
static unsigned int ready_w, ready_h, ready_stride;
static atomic_t pad_bits;		/* BIT(K_...) of the fingers on the pad */

static lv_obj_t *pad_objs[K_COUNT], *msg_label, *load_label, *gba_only_l, *gba_only_r;
static lv_timer_t *poll;
static const char *origin = "files";

/* ---- sound --------------------------------------------------------------------------------- */

static void audio_main(void *a, void *b, void *c)
{
	struct i2s_config cfg = {
		.word_size = 16,
		.channels = 2,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
		.frame_clk_freq = RATE,
		.mem_slab = &slab,
		.block_size = BLOCK_BYTES,
		.timeout = 2000,
	};
	bool started = false;
	int queued = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	if (i2s_configure(i2s, I2S_DIR_TX, &cfg) != 0) {
		printk("game: i2s_configure failed\n");
		return;
	}
	audio_codec_start_output(ctl);
	audio_codec_set_property(ctl, AUDIO_PROPERTY_OUTPUT_VOLUME, AUDIO_CHANNEL_ALL,
				 (audio_property_value_t){.vol = 150});

	while (!quit) {
		void *blk;
		int ret;

		if (ring_buf_size_get(&ring) < BLOCK_BYTES) {
			k_msleep(1);
			continue;
		}
		if (k_mem_slab_alloc(&slab, &blk, K_MSEC(200)) != 0) {
			continue;
		}
		ring_buf_get(&ring, blk, BLOCK_BYTES);
		ret = i2s_write(i2s, blk, BLOCK_BYTES);
		if (ret == -EIO) {
			/* the stream ran dry: start it again */
			started = false;
			queued = 0;
			k_mem_slab_free(&slab, blk);
			i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_PREPARE);
			continue;
		} else if (ret != 0) {
			k_mem_slab_free(&slab, blk);
			continue;
		}
		if (!started && ++queued >= PREFILL &&
		    i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_START) == 0) {
			started = true;
		}
	}
	i2s_trigger(i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
	/* the amplifier and the headphone driver off, or they hiss with nothing playing */
	audio_codec_stop_output(ctl);
	printk("game: sound output off\n");
}

static size_t audio_queued(void)
{
	return ring_buf_size_get(&ring) / 4;
}

/* ---- picture ------------------------------------------------------------------------------- */

/* showing a picture can wait for the refresh: not in the emulator thread, only the newest is shown */
static void video_main(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	while (!quit) {
		struct display_sunxi_rgb img = {.width = 0};

		if (k_sem_take(&frame_sem, K_MSEC(200)) != 0) {
			continue;
		}
		img.data = buf[atomic_get(&ready_idx)];
		img.width = ready_w;
		img.height = ready_h;
		img.stride = ready_stride;
		img.nonblock = false;
		display_sunxi_show_rgb(disp, &img);
	}
}

static uint16_t *video_acquire(void)
{
	return (uint16_t *)buf[(cur + 1) % NBUF];
}

/*
 * The rows of the picture are NES: 256 pixels apart; Game Boy and Game Boy Advance: 240 apart (a
 * Game Boy picture of 160 pixels sits in the left of them), whatever the width of the picture.
 */
static void video_present(unsigned int w, unsigned int h)
{
	unsigned int stride = (core == CORE_NES ? 256 : 240) * 2;

	cur = (cur + 1) % NBUF;
	sys_cache_data_flush_range(buf[cur], stride * h);
	ready_stride = stride;
	ready_w = w;
	ready_h = h;
	atomic_set(&ready_idx, cur);
	k_sem_give(&frame_sem);
}

/* ---- keys ---------------------------------------------------------------------------------- */

static uint32_t keys_gba(uint32_t k)
{
	static const uint32_t map[K_COUNT] = {
		[K_A] = GBA_KEY_A, [K_B] = GBA_KEY_B, [K_SELECT] = GBA_KEY_SELECT,
		[K_START] = GBA_KEY_START, [K_UP] = GBA_KEY_UP, [K_DOWN] = GBA_KEY_DOWN,
		[K_LEFT] = GBA_KEY_LEFT, [K_RIGHT] = GBA_KEY_RIGHT, [K_L] = GBA_KEY_L,
		[K_R] = GBA_KEY_R,
	};
	uint32_t r = 0;

	for (int i = 0; i < K_COUNT; i++) {
		if (k & BIT(i)) {
			r |= map[i];
		}
	}

	return r;
}

static uint32_t keys_nes(uint32_t k)
{
	static const uint32_t map[K_COUNT] = {
		[K_A] = NESEMU_KEY_A, [K_B] = NESEMU_KEY_B, [K_SELECT] = NESEMU_KEY_SELECT,
		[K_START] = NESEMU_KEY_START, [K_UP] = NESEMU_KEY_UP, [K_DOWN] = NESEMU_KEY_DOWN,
		[K_LEFT] = NESEMU_KEY_LEFT, [K_RIGHT] = NESEMU_KEY_RIGHT,
	};
	uint32_t r = 0;

	for (int i = 0; i < K_COUNT; i++) {
		if (k & BIT(i)) {
			r |= map[i];
		}
	}

	return r;
}

/* ---- the emulator thread ------------------------------------------------------------------- */

static uint32_t pad_mask(void);

static void emu_main(void *a, void *b, void *c)
{
	static int16_t pcm[2 * 2048];
	int64_t t_save, t_report;
	uint32_t frames = 0, emu_us = 0, skipped = 0;
	bool catching_up = false;
	int ret;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	ret = core == CORE_NES ? nesemu_open(rom_path, save_path, RATE)
			       : gba_open(rom_path, save_path, RATE);
	if (ret != 0) {
		printk("game: cannot start %s: %d\n", rom_path, ret);
		status = ret;
		return;
	}
	running = true;
	status = 1;
	t_save = t_report = k_uptime_get();
	while (!quit) {
		uint32_t c0 = k_cycle_get_32();
		uint32_t keys = pad_mask();
		size_t n;
		int drawn;

		if (core == CORE_NES) {
			nesemu_set_keys(keys_nes(keys));
			nesemu_set_frame_buffer(video_acquire());
			/* a frame that runs late is not drawn, the sound has priority */
			nesemu_set_frameskip(audio_queued() < RATE / 30 ? 1 : 0);
			drawn = nesemu_run_frame();
			if (drawn) {
				video_present(NESEMU_WIDTH, nesemu_height());
			}
		} else {
			size_t queued = audio_queued();

			gba_set_keys(keys_gba(keys));
			gba_set_frame_buffer(video_acquire());
			if (queued < SKIP_BELOW) {
				catching_up = true;
			} else if (queued > SKIP_RESUME) {
				catching_up = false;
			}
			/* the picture is drawn at 30 fps; the emulation and sound run at full rate */
			gba_set_frameskip(catching_up ? 3 : 1);
			drawn = gba_run_frame();
			if (drawn) {
				unsigned int w, h;

				gba_frame(&w, &h);
				video_present(w, h);
			}
		}
		emu_us += k_cyc_to_us_floor32(k_cycle_get_32() - c0);
		frames++;
		skipped += drawn ? 0 : 1;
		while ((n = core == CORE_NES ? nesemu_audio_read(pcm, ARRAY_SIZE(pcm) / 2)
					     : gba_audio_read(pcm, ARRAY_SIZE(pcm) / 2)) > 0) {
			size_t done = 0;

			while (done < n && !quit) {
				done += ring_buf_put(&ring, (const uint8_t *)(pcm + done * 2),
						     (n - done) * 4) / 4;
				if (done < n) {
					k_msleep(1);
				}
			}
		}
		while (audio_queued() > HIGH_WATER && !quit) {
			k_msleep(1);
		}
		if (k_uptime_get() - t_report >= 5000) {
			printk("game: %u frames in 5 s, emulation %u us per frame, %u not drawn, sound queued %u\n",
			       frames, emu_us / MAX(frames, 1U), skipped, (unsigned)audio_queued());
#ifdef CONFIG_DEMAND_PAGING_STATS
			{
				static unsigned long last_faults;
				struct k_mem_paging_stats_t st;

				k_mem_paging_stats_get(&st);
				printk("game: %lu page faults in 5 s, %lu clean and %lu dirty pages evicted so far\n",
				       st.pagefaults.cnt - last_faults, st.eviction.clean,
				       st.eviction.dirty);
				last_faults = st.pagefaults.cnt;
			}
#endif
			frames = emu_us = skipped = 0;
			t_report = k_uptime_get();
		}
		if (k_uptime_get() - t_save >= SAVE_MS) {
			if (core == CORE_NES) {
				nesemu_save_flush();
			} else {
				gba_save_flush();
			}
			t_save = k_uptime_get();
		}
	}
	if (core == CORE_NES) {
		nesemu_save_flush();
		nesemu_close();
	} else {
		gba_save_flush();
		gba_close();
	}
	running = false;
}

/* ---- touch --------------------------------------------------------------------------------- */

#define TOUCH_SLOTS	5
#define HIT_MARGIN	12
#define RELEASE_HOLD_MS	50

static struct pad_key {
	int x, y, w, h;
	bool shown;
	uint32_t hold_until;
} keytab[K_COUNT];

static struct {
	int x, y;
	bool down;
} fingers[TOUCH_SLOTS];
static int cur_slot;

static void touch_event(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);
	switch (evt->code) {
	case INPUT_ABS_MT_SLOT:
		cur_slot = CLAMP(evt->value, 0, TOUCH_SLOTS - 1);
		break;
	case INPUT_ABS_X:
		fingers[cur_slot].x = evt->value;
		break;
	case INPUT_ABS_Y:
		fingers[cur_slot].y = evt->value;
		break;
	case INPUT_BTN_TOUCH:
		fingers[cur_slot].down = evt->value != 0;
		break;
	}
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), touch_event, NULL);

/*
 * Which keys have a finger on them. Called by the emulation thread before every frame, so the
 * keys do not depend on the user interface thread, which has no CPU to spare while the game
 * runs. A key stays down for a short time after the finger left it (RELEASE_HOLD_MS).
 */
static uint32_t pad_mask(void)
{
	uint32_t mask = 0, now = k_uptime_get_32();

	for (int k = 0; k < K_COUNT; k++) {
		struct pad_key *key = &keytab[k];
		bool pressed = false;

		if (key->w == 0 || (core == CORE_NES && (k == K_L || k == K_R))) {
			continue;
		}
		for (int f = 0; f < TOUCH_SLOTS; f++) {
			if (fingers[f].down && fingers[f].x >= key->x - HIT_MARGIN &&
			    fingers[f].x < key->x + key->w + HIT_MARGIN &&
			    fingers[f].y >= key->y - HIT_MARGIN &&
			    fingers[f].y < key->y + key->h + HIT_MARGIN) {
				pressed = true;
			}
		}
		if (pressed) {
			key->hold_until = now + RELEASE_HOLD_MS;
		} else if ((int32_t)(now - key->hold_until) < 0) {
			pressed = true;
		}
		if (pressed) {
			mask |= BIT(k);
		}
	}
	atomic_set(&pad_bits, mask);

	return mask;
}

/* the state of the start-up and of an error; shows the keys that are down, for the eye only */
static void touch_scan(lv_timer_t *t)
{
	uint32_t mask = (uint32_t)atomic_get(&pad_bits);

	ARG_UNUSED(t);
	for (int k = 0; k < K_COUNT; k++) {
		struct pad_key *key = &keytab[k];
		bool pressed = (mask & BIT(k)) != 0U;

		if (key->w == 0) {
			continue;
		}
		if (pressed != key->shown) {
			if (pressed) {
				lv_obj_add_state(pad_objs[k], LV_STATE_PRESSED);
			} else {
				lv_obj_remove_state(pad_objs[k], LV_STATE_PRESSED);
			}
			key->shown = pressed;
		}
	}

	if (status < 0 && lv_obj_has_flag(msg_label, LV_OBJ_FLAG_HIDDEN)) {
		lv_label_set_text_fmt(msg_label, LV_SYMBOL_WARNING "\n%s",
				      status == -EFBIG ? "This game is too large for the memory" :
				      status == -ENOMEM ? "Not enough memory for this game" :
				      status == -ENOTSUP ? "This cartridge is not supported" :
				      "This game cannot be started");
		lv_obj_remove_flag(msg_label, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(load_label, LV_OBJ_FLAG_HIDDEN);
	}
	if (status == 1 && !lv_obj_has_flag(load_label, LV_OBJ_FLAG_HIDDEN)) {
		lv_obj_add_flag(load_label, LV_OBJ_FLAG_HIDDEN);
	}
}

/* ---- the screen ---------------------------------------------------------------------------- */

static void key_add(lv_obj_t *parent, int k, int x, int y, int w, int h, const char *text,
		    int radius)
{
	lv_obj_t *b = lv_obj_create(parent);
	lv_obj_t *l = lv_label_create(b);

	lv_obj_remove_style_all(b);
	lv_obj_set_size(b, w, h);
	lv_obj_set_pos(b, x, y);
	lv_obj_set_style_radius(b, radius, 0);
	lv_obj_set_style_bg_color(b, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(b, LV_OPA_30, 0);
	lv_obj_set_style_bg_color(b, lv_color_make(255, 200, 60), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(b, LV_OPA_70, LV_STATE_PRESSED);
	lv_obj_set_style_border_color(b, lv_color_white(), 0);
	lv_obj_set_style_border_opa(b, LV_OPA_50, 0);
	lv_obj_set_style_border_width(b, 2, 0);
	lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(l, lv_color_white(), 0);
	lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
	lv_label_set_text(l, text);
	lv_obj_center(l);
	pad_objs[k] = b;
	keytab[k] = (struct pad_key){.x = x, .y = y, .w = w, .h = h};
}

static void exit_clicked(lv_event_t *e)
{
	ARG_UNUSED(e);
	if (strcmp(origin, "files") == 0) {
		char dir[sizeof(rom_path)];
		char *slash;

		strcpy(dir, rom_path);
		slash = strrchr(dir, '/');
		if (slash != NULL) {
			*slash = '\0';
		}
		ui_goto_arg("files", dir);
	} else {
		ui_goto(origin);
	}
}

static lv_obj_t *game_create(void)
{
	lv_obj_t *scr = ui_screen_new(true);
	lv_obj_t *x = ui_icon_button(scr, LV_SYMBOL_CLOSE, 64);

	lv_obj_set_pos(x, 24, 24);
	lv_obj_add_event_cb(x, exit_clicked, LV_EVENT_CLICKED, NULL);

	/*
	 * The picture fills the width of the 1024x768 panel (a Game Boy Advance 1024x683, a NES
	 * 1024x896 is cut to the height by the plane), the keys are drawn over it.
	 */
	key_add(scr, K_LEFT, 30, 560, 90, 90, LV_SYMBOL_LEFT, 20);
	key_add(scr, K_RIGHT, 210, 560, 90, 90, LV_SYMBOL_RIGHT, 20);
	key_add(scr, K_UP, 120, 470, 90, 90, LV_SYMBOL_UP, 20);
	key_add(scr, K_DOWN, 120, 650, 90, 90, LV_SYMBOL_DOWN, 20);
	key_add(scr, K_A, 890, 560, 100, 100, "A", LV_RADIUS_CIRCLE);
	key_add(scr, K_B, 770, 630, 100, 100, "B", LV_RADIUS_CIRCLE);
	key_add(scr, K_L, 110, 24, 140, 70, "L", 16);
	key_add(scr, K_R, 774, 24, 140, 70, "R", 16);
	key_add(scr, K_SELECT, 392, 700, 120, 56, "SELECT", 28);
	key_add(scr, K_START, 532, 700, 120, 56, "START", 28);
	gba_only_l = pad_objs[K_L];
	gba_only_r = pad_objs[K_R];

	load_label = lv_label_create(scr);
	lv_obj_set_style_text_color(load_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(load_label, &lv_font_montserrat_28, 0);
	lv_label_set_text(load_label, "Loading...");
	lv_obj_align(load_label, LV_ALIGN_CENTER, 0, -40);

	msg_label = lv_label_create(scr);
	lv_obj_set_style_text_color(msg_label, lv_color_hex(C_WARN), 0);
	lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_bg_color(msg_label, lv_color_hex(C_BG), 0);
	lv_obj_set_style_bg_opa(msg_label, LV_OPA_80, 0);
	lv_obj_set_style_pad_all(msg_label, 24, 0);
	lv_obj_set_style_radius(msg_label, 20, 0);
	lv_obj_align(msg_label, LV_ALIGN_CENTER, 0, -40);
	lv_obj_add_flag(msg_label, LV_OBJ_FLAG_HIDDEN);

	return scr;
}

static bool ext_is(const char *p, const char *ext)
{
	size_t n = strlen(p), e = strlen(ext);

	if (n <= e) {
		return false;
	}
	for (size_t i = 0; i < e; i++) {
		char a = p[n - e + i], b = ext[i];

		if ((a >= 'A' && a <= 'Z' ? a + 32 : a) != b) {
			return false;
		}
	}

	return true;
}

void ui_game_open(const char *path, const char *from)
{
	origin = from;
	ui_goto_arg("game", (void *)path);
}

static void game_file_open(const char *path)
{
	ui_game_open(path, "files");
}

static const char *const nes_exts[] = {".nes", NULL};
static const char *const gb_exts[] = {".gb", ".gbc", ".gba", NULL};

MP5_FILE_HANDLER(nes, .kind = "nes", .title = "NES", .symbol = LV_SYMBOL_KEYBOARD,
		 .color = &ui_pal.pink, .exts = nes_exts, .open = game_file_open);
MP5_FILE_HANDLER(gb, .kind = "gb", .title = "Game Boy", .symbol = LV_SYMBOL_KEYBOARD,
		 .color = &ui_pal.pink, .exts = gb_exts, .open = game_file_open);
MP5_HOME_TILE(nes, .order = 50, .symbol = LV_SYMBOL_KEYBOARD, .title = "NES",
	      .subtitle = "Famicom games", .color_top = 0xE5484D, .color_bottom = 0xB02A37,
	      .kind = "nes");
MP5_HOME_TILE(gb, .order = 60, .symbol = LV_SYMBOL_PLAY, .title = "Game Boy",
	      .subtitle = "GB, GBC and GBA", .color_top = 0x9B6CFF, .color_bottom = 0x6A3FD0,
	      .kind = "gb");

static void game_enter(void *arg)
{
	const char *path = arg;
	char *dot;

	if (path == NULL || !device_is_ready(i2s) || !device_is_ready(ctl)) {
		status = -ENODEV;
		return;
	}
	strncpy(rom_path, path, sizeof(rom_path) - 1);
	rom_path[sizeof(rom_path) - 1] = '\0';
	core = ext_is(rom_path, ".nes") ? CORE_NES : CORE_GBA;
	strncpy(save_path, rom_path, sizeof(save_path) - 5);
	save_path[sizeof(save_path) - 5] = '\0';
	dot = strrchr(save_path, '.');
	strcpy(dot != NULL && dot > strrchr(save_path, '/') ? dot : save_path + strlen(save_path),
	       ".sav");

	status = 0;
	quit = false;
	cur = 0;
	ring_buf_reset(&ring);
	lv_obj_remove_flag(load_label, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(msg_label, LV_OBJ_FLAG_HIDDEN);
	if (core == CORE_NES) {
		lv_obj_add_flag(gba_only_l, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(gba_only_r, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_remove_flag(gba_only_l, LV_OBJ_FLAG_HIDDEN);
		lv_obj_remove_flag(gba_only_r, LV_OBJ_FLAG_HIDDEN);
	}
	for (int i = 0; i < NBUF; i++) {
		buf[i] = aligned_alloc(64, BUF_SIZE);
		if (buf[i] == NULL) {
			status = -ENOMEM;
			return;
		}
		memset(buf[i], 0, BUF_SIZE);
	}
	k_thread_create(&game_video_thread, game_video_stack, K_THREAD_STACK_SIZEOF(game_video_stack), video_main,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&game_video_thread, "game_video");
	k_thread_create(&game_audio_thread, game_audio_stack, K_THREAD_STACK_SIZEOF(game_audio_stack), audio_main,
			NULL, NULL, NULL, 3, 0, K_NO_WAIT);
	k_thread_name_set(&game_audio_thread, "game_audio");
	k_thread_create(&emu_thread, game_emu_stack, K_THREAD_STACK_SIZEOF(game_emu_stack), emu_main, NULL,
			NULL, NULL, EMU_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&emu_thread, "game_emu");
	mp5_profile_start(&emu_thread);
	threads_started = true;
	mp5_background_hold(true);
	if (poll == NULL) {
		poll = lv_timer_create(touch_scan, 33, NULL);
	}
	lv_timer_resume(poll);
}

static void game_leave(void)
{
	if (poll != NULL) {
		lv_timer_pause(poll);
	}
	quit = true;
	if (threads_started) {
		threads_started = false;
		k_thread_join(&emu_thread, K_SECONDS(5));
		k_thread_join(&game_audio_thread, K_SECONDS(5));
		k_thread_join(&game_video_thread, K_SECONDS(5));
	}
	mp5_background_hold(false);
	display_sunxi_hide_yuv(disp);
	for (int i = 0; i < NBUF; i++) {
		free(buf[i]);
		buf[i] = NULL;
	}
	atomic_set(&pad_bits, 0);
	for (int k = 0; k < K_COUNT; k++) {
		keytab[k].shown = false;
		if (pad_objs[k] != NULL) {
			lv_obj_remove_state(pad_objs[k], LV_STATE_PRESSED);
		}
	}
}

MP5_SCREEN(game, "game", game_create, game_enter, game_leave);

/* CONFIG_MP5_GAME starts a game at boot (for the check of the emulators) */
static int game_autostart(void)
{
	if (sizeof(CONFIG_MP5_GAME) > 1) {
		ui_game_open(CONFIG_MP5_GAME, "home");
	}

	return 0;
}
MP5_INIT(MP5_STAGE_READY, 50, "game autostart", game_autostart);

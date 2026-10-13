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
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app.h"
#include "lvgl_mem.h"
#include "cover.h"
#include "features/video/player.h"
#include "ui_priv.h"

#define TICK_MS		250
#define SCROLL_STEP	320
#define MEM_TOLERANCE	256U	/* KiB */

enum phase {
	P_LIST, P_SCROLL, P_TAB, P_SEARCH, P_PLAY, P_DONE,
};

static const char *const names[] = {"list", "scroll", "popular", "search", "play", "done"};
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

static void memory(const char *tag, bool reference)
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
	} else if (have_ref) {
		check(free_kib + MEM_TOLERANCE >= ref_free, "the free memory is as at the reference");
		check(biggest + MEM_TOLERANCE >= ref_biggest, "the biggest block is as at the reference");
		check(lv_used <= ref_lv_used + MEM_TOLERANCE / 2, "the LVGL heap is as at the reference");
	}
	check(lv_free >= 64, "the LVGL heap has 64 KiB free");
}

static void list_phase(void)
{
	struct list_info info;

	app_list_info(LIST_RECOMMEND, &info);
	if (info.count > 0) {
		printk("selftest: the recommended list has %d videos\n", info.count);
		check(true, "the recommended list loaded");
		memory("start", false);
		round_no = 0;
		to(P_SCROLL);
	} else if (k_uptime_get() - since > 120000) {
		check(false, "the recommended list loaded");
		to(P_SEARCH);
	}
}

/* down to the end of the list (and the pages it loads), up to its top */
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
		memory("both tabs built", false);
		ui_home_tab(0);
		to(P_SEARCH);
	}
}

static void search_phase(void)
{
	struct list_info info;
	static uint32_t gen;
	char what[96];

	if (step >= ARRAY_SIZE(words)) {
		memory("after the searches", false);
		to(P_PLAY);
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
		app_cmd(CMD_PLAY, LIST_RECOMMEND << 16 | index);
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
		if (now - t0 >= 12000) {
			uint32_t moved = player_position_ms() - pos0;

			snprintf(what, sizeof(what), "video %d plays on (%u ms in 12 s)", index, moved);
			check(player_state() == PLAYER_PLAYING && moved > 9000, what);
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
	case P_SCROLL:
		scroll_phase();
		break;
	case P_TAB:
		tab_phase();
		break;
	case P_SEARCH:
		search_phase();
		break;
	case P_PLAY:
		play_phase();
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

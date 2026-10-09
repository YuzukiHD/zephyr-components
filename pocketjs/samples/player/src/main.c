/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * PocketJS on the F101: the embedded app runs in the QuickJS guest, every tick the UI core
 * produces a frame, the damaged rectangles are drawn into an RGB565 buffer and the buffer
 * is put on the scaling video plane.
 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/dt-bindings/clock/sun252i-f101-ccu.h>

#include "pocketjs/guest.h"
#include "pocketjs/guest_quickjs.h"
#include "pocketjs/package.h"
#include "pocketjs/render_rgb565.h"
#include "pocketjs/ui_core.h"
#include "pocketjs/ui_qjs.h"
#include "input.h"
#ifdef CONFIG_POCKETJS_G2D
#include "pocketjs_zephyr/g2d_accel.h"
#endif
#include "pocketjs_package_player.h"

/* two thirds of the main stack, the rest is for the native code below the interpreter */
#define JS_STACK_LIMIT (CONFIG_MAIN_STACK_SIZE / 3 * 2)
#define CONTRACT pocketjs_package_player_contract
#define PACKAGE	 pocketjs_package_player

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

#define NPIC 4

static uint8_t *pic[NPIC];
static uint32_t pic_w, pic_h;
static size_t pic_stride;
static atomic_t ready_idx;
K_SEM_DEFINE(frame_sem, 0, 1);
K_THREAD_STACK_DEFINE(video_stack, 4096);
static struct k_thread video_thread;

/*
 * Putting a picture up waits for the refresh; that must not hold the tick. Only the newest
 * picture is shown, and the ring is longer than what the plane and the thread can hold.
 */
static void video_main(void *a, void *b, void *c)
{
	bool warned = false;

	while (true) {
		struct display_sunxi_rgb img = {.xrgb8888 = false};
		int ret;

		k_sem_take(&frame_sem, K_FOREVER);
		img.data = pic[atomic_get(&ready_idx)];
		img.width = pic_w;
		img.height = pic_h;
		img.stride = pic_stride;
		ret = display_sunxi_show_rgb(disp, &img);
		if (ret != 0 && !warned) {
			printk("[E] pocketjs_player: show_rgb failed: %d\n", ret);
			warned = true;
		}
	}
}

/* A busy loop over a checksummed block of memory: a CPU that is too fast corrupts it */
static bool cpu_selftest(void)
{
	static uint32_t blk[16 * 1024];
	uint32_t sum = 0, ref = 0;

	for (unsigned int i = 0; i < ARRAY_SIZE(blk); i++) {
		blk[i] = i * 2654435761U;
	}
	for (int round = 0; round < 50; round++) {
		sum = 0;
		for (unsigned int i = 0; i < ARRAY_SIZE(blk); i++) {
			sum = (sum << 1 | sum >> 31) ^ blk[i] ^ (sum * 3U);
		}
		if (round == 0) {
			ref = sum;
		} else if (sum != ref) {
			return false;
		}
	}
	return true;
}

/* In steps of 72 MHz, the memory is checked after each one; the voltage is not changed. */
static void cpu_clock_setup(void)
{
	const struct device *cctl = DEVICE_DT_GET(DT_NODELABEL(cctl));
	uint32_t rate, target = CONFIG_SAMPLE_POCKETJS_CPU_MHZ * 1000000U;

	clock_control_get_rate(cctl, (clock_control_subsys_t)CLK_CPU, &rate);
	while (target != 0U && rate < target) {
		uint32_t next = MIN(rate + 72000000U, target);

		if (clock_control_set_rate(cctl, (clock_control_subsys_t)CLK_CPU,
					   (clock_control_subsys_rate_t)next) != 0) {
			break;
		}
		if (!cpu_selftest()) {
			printk("[W] cpu: %u MHz BAD, back to %u MHz\n", next / 1000000U,
			       rate / 1000000U);
			clock_control_set_rate(cctl, (clock_control_subsys_t)CLK_CPU,
					       (clock_control_subsys_rate_t)rate);
			return;
		}
		rate = next;
	}
	printk("cpu: %u MHz\n", rate / 1000000U);
}

#ifdef CONFIG_SAMPLE_POCKETJS_PROFILE
void profile_start(struct k_thread *target);
#endif

static int check(esp_err_t err, const char *what)
{
	if (err != ESP_OK) {
		printk("[E] pocketjs_player: %s failed: 0x%x\n", what, err);
		return -1;
	}
	return 0;
}

#define CHECK(x) do { if (check((x), #x) != 0) { return 0; } } while (0)

int main(void)
{
	if (!device_is_ready(disp)) {
		printk("[E] pocketjs_player: display not ready\n");
		return 0;
	}
	cpu_clock_setup();
#ifdef CONFIG_SAMPLE_POCKETJS_PROFILE
	profile_start(k_current_get());
#endif

	pocketjs_package_t *package = NULL;
	pocketjs_package_variant_t app = {.struct_size = sizeof(app)};

	CHECK(pocketjs_package_open(PACKAGE.data, PACKAGE.size, 0, &package));
	CHECK(pocketjs_package_select(package, &CONTRACT, &app));

	pocketjs_guest_config_t guest_config;
	pocketjs_guest_t *guest = NULL;

	pocketjs_guest_config_defaults(&guest_config);
	CHECK(pocketjs_guest_create(&guest_config, &guest));
	/* QuickJS assumes 1 MiB of stack by default; the main thread has CONFIG_MAIN_STACK_SIZE,
	 * of which the interpreter may use JS_STACK_LIMIT before it throws a RangeError.
	 */
	JS_SetMaxStackSize(JS_GetRuntime(pocketjs_guest_quickjs_context(guest)), JS_STACK_LIMIT);

	pocketjs_ui_core_config_t core_config;
	pocketjs_ui_core_t *core = NULL;

	pocketjs_ui_core_config_defaults(&core_config);
	core_config.logical_width = CONTRACT.logical_width;
	core_config.logical_height = CONTRACT.logical_height;
	core_config.raster_density = CONTRACT.raster_density;
	core_config.tick_hz = CONTRACT.tick_hz;
	CHECK(pocketjs_ui_core_create(&core_config, &core));

	const pocketjs_ui_qjs_config_t binding_config = {
		.struct_size = sizeof(binding_config),
		.target_id = CONTRACT.target_id,
		.host_abi = CONTRACT.host_abi,
	};
	pocketjs_ui_qjs_t *binding = NULL;

	CHECK(pocketjs_ui_qjs_create(guest, core, &binding_config, &binding));
	CHECK(pocketjs_ui_qjs_feed_pak(binding, app.pak.data, app.pak.size));
	CHECK(pocketjs_ui_qjs_mount(binding));
	CHECK(pocketjs_guest_eval(guest, (const char *)app.javascript.data,
				  app.javascript.size - 1U, "player"));

	pocketjs_rgb565_renderer_config_t renderer_config;
	pocketjs_rgb565_renderer_t *renderer = NULL;
	pocketjs_rgb565_target_t *target = NULL;

	pocketjs_rgb565_renderer_config_defaults(&renderer_config);
	renderer_config.scale = CONTRACT.raster_density;
#ifdef CONFIG_POCKETJS_G2D
	pocketjs_g2d_t *g2d = pocketjs_g2d_create(NULL);
	const pocketjs_rgb565_accelerator_t *accel = pocketjs_g2d_accelerator(g2d);

	if (g2d != NULL) {
		renderer_config.min_fill_pixels = pocketjs_g2d_min_fill(g2d);
		renderer_config.min_blend_pixels = pocketjs_g2d_min_blend(g2d);
	} else {
		printk("[W] pocketjs_player: no G2D, software rendering\n");
	}
#else
	const pocketjs_rgb565_accelerator_t *accel = NULL;
#endif
	CHECK(pocketjs_rgb565_renderer_create(&renderer_config, &renderer));
	CHECK(pocketjs_rgb565_target_create(&target));

	/*
	 * The canvas keeps the whole picture and is drawn into by the damaged strips; every
	 * changed tick it is copied into the next picture of the ring, which the video thread
	 * shows. A picture that is on the plane is never written.
	 */
	pic_w = CONTRACT.logical_width * CONTRACT.raster_density;
	pic_h = CONTRACT.logical_height * CONTRACT.raster_density;
	pic_stride = pic_w * 2;
	const size_t size = ROUND_UP(pic_stride * pic_h, 64);
	uint8_t *canvas = aligned_alloc(64, size);

	if (canvas == NULL) {
		printk("[E] pocketjs_player: out of memory\n");
		return 0;
	}
	memset(canvas, 0, size);
	for (int i = 0; i < NPIC; i++) {
		pic[i] = aligned_alloc(64, size);
		if (pic[i] == NULL) {
			printk("[E] pocketjs_player: out of memory\n");
			return 0;
		}
		memset(pic[i], 0, size);
	}
	input_init(CONTRACT.logical_width, CONTRACT.logical_height);
	k_thread_create(&video_thread, video_stack, K_THREAD_STACK_SIZEOF(video_stack), video_main,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&video_thread, "video");

	const int64_t period_us = 1000000 / CONTRACT.tick_hz;
	int64_t next = k_ticks_to_us_floor64(k_uptime_ticks());
	unsigned int cur = 0, frames = 0, shown = 0;
	int64_t report = next;
	unsigned int ppa_ops = 0, last_touches = 0;
	uint32_t last_buttons = 0;
	int64_t busy_sum = 0, busy_max = 0, t_turn = 0, t_render = 0, t_copy = 0;

	printk("pocketjs_player: %s %ux%u at %u Hz, guest %u KiB\n", CONTRACT.target_id,
	       (unsigned int)pic_w, (unsigned int)pic_h, (unsigned int)CONTRACT.tick_hz,
	       (unsigned int)(app.javascript.size / 1024));

	while (true) {
		const int64_t started = k_ticks_to_us_floor64(k_uptime_ticks());
		pocketjs_ui_frame_view_t frame = {.struct_size = sizeof(frame)};
		pocketjs_ui_input_t input = {.struct_size = sizeof(input)};
		pocketjs_ui_touch_t touches[INPUT_MAX_TOUCHES];
		pocketjs_rgb565_damage_plan_t plan = {.struct_size = sizeof(plan)};

		input_sample(&input, touches);
		if (input.buttons != last_buttons || input.touch_count != last_touches) {
			printk("input: buttons 0x%04x, %u finger(s)\n", input.buttons,
			       (unsigned int)input.touch_count);
			last_buttons = input.buttons;
			last_touches = input.touch_count;
		}
		CHECK(pocketjs_ui_turn(binding, &input, &frame));
		const int64_t t1 = k_ticks_to_us_floor64(k_uptime_ticks());

		CHECK(pocketjs_rgb565_prepare(renderer, target, &frame, &plan));
		frames++;

		if (plan.region_count != 0) {
			for (uint32_t i = 0; i < plan.region_count; i++) {
				const pocketjs_rgb565_rect_t region = plan.regions[i];
				pocketjs_rgb565_render_stats_t stats = {.struct_size = sizeof(stats)};

				/* a strip is full width: its rows lie in the canvas as they are */
				CHECK(pocketjs_rgb565_render_strip(
					renderer, &frame,
					(uint16_t *)(canvas + (size_t)region.y * pic_stride),
					(size_t)pic_w * region.height, region, accel, &stats));
				ppa_ops += stats.ppa_fills + stats.ppa_blends;
			}
			CHECK(pocketjs_rgb565_commit(renderer, target, &frame));

			const int64_t t2 = k_ticks_to_us_floor64(k_uptime_ticks());

			t_render += t2 - t1;
			cur = (cur + 1) % NPIC;
			memcpy(pic[cur], canvas, pic_stride * pic_h);
			sys_cache_data_flush_range(pic[cur], pic_stride * pic_h);
			atomic_set(&ready_idx, cur);
			k_sem_give(&frame_sem);
			t_copy += k_ticks_to_us_floor64(k_uptime_ticks()) - t2;
			shown++;
		}

		next += period_us;
		const int64_t now = k_ticks_to_us_floor64(k_uptime_ticks());

		t_turn += t1 - started;
		busy_sum += now - started;
		busy_max = MAX(busy_max, now - started);
		if (next > now) {
			k_usleep(next - now);
		} else if (now - next > 4 * period_us) {
			next = now;
		}
		if (now - report >= 5000000) {
			printk("pocketjs_player: %u ticks, %u pictures in 5 s, busy %u us avg %u max "
			       "(turn %u, render %u, copy %u) g2d %u\n",
			       frames, shown, (unsigned int)(busy_sum / MAX(frames, 1U)),
			       (unsigned int)busy_max, (unsigned int)(t_turn / MAX(frames, 1U)),
			       (unsigned int)(t_render / MAX(shown, 1U)),
			       (unsigned int)(t_copy / MAX(shown, 1U)), ppa_ops);
			ppa_ops = 0;
			frames = shown = 0;
			busy_sum = busy_max = t_turn = t_render = t_copy = 0;
			report = now;
		}
	}
	return 0;
}

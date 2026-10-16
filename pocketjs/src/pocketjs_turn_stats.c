/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Time of the three parts of a turn: the script frame, the tick and the draw of the UI core.
 * The calls are intercepted by the linker (--wrap), the submodule is not touched.
 */

#include <zephyr/kernel.h>

#include "pocketjs/guest.h"
#include "pocketjs/ui_core.h"
#include "pocketjs_zephyr/turn_stats.h"

esp_err_t __real_pocketjs_guest_frame(pocketjs_guest_t *guest,
				      const pocketjs_guest_frame_t *frame);
void __real_pocketjs_ui_core_tick(pocketjs_ui_core_t *core);
esp_err_t __real_pocketjs_ui_core_draw(pocketjs_ui_core_t *core,
				       pocketjs_ui_frame_view_t *out_frame);

static struct pocketjs_turn_stats stats;

/* the cycle and instruction counters of the core, 32 bit deltas are enough for a frame */
#define CSR_READ(name) ({ unsigned long v_; __asm__ volatile("csrr %0, " name : "=r"(v_)); v_; })

static inline int64_t now_us(void)
{
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

esp_err_t __wrap_pocketjs_guest_frame(pocketjs_guest_t *guest,
				      const pocketjs_guest_frame_t *frame)
{
	const int64_t t0 = now_us();
	const unsigned long c0 = CSR_READ("mcycle"), i0 = CSR_READ("minstret");
	esp_err_t ret = __real_pocketjs_guest_frame(guest, frame);

	stats.script_cycles += (uint32_t)(CSR_READ("mcycle") - c0);
	stats.script_instrs += (uint32_t)(CSR_READ("minstret") - i0);
	stats.script_us += now_us() - t0;
	return ret;
}

void __wrap_pocketjs_ui_core_tick(pocketjs_ui_core_t *core)
{
	const int64_t t0 = now_us();

	__real_pocketjs_ui_core_tick(core);
	stats.tick_us += now_us() - t0;
}

esp_err_t __wrap_pocketjs_ui_core_draw(pocketjs_ui_core_t *core,
				       pocketjs_ui_frame_view_t *out_frame)
{
	const int64_t t0 = now_us();
	esp_err_t ret = __real_pocketjs_ui_core_draw(core, out_frame);

	stats.draw_us += now_us() - t0;
	stats.turns++;
	return ret;
}

struct pocketjs_turn_stats pocketjs_turn_stats_take(void)
{
	struct pocketjs_turn_stats out = stats;

	stats = (struct pocketjs_turn_stats){0};
	return out;
}

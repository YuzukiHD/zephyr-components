/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POCKETJS_ZEPHYR_TURN_STATS_H_
#define POCKETJS_ZEPHYR_TURN_STATS_H_

#include <stdint.h>

/** Microseconds spent in the parts of the turns since the last take, and how many turns */
struct pocketjs_turn_stats {
	int64_t script_us;
	int64_t tick_us;
	int64_t draw_us;
	/** core cycles and retired instructions inside the script frames */
	uint64_t script_cycles;
	uint64_t script_instrs;
	uint32_t turns;
};

/** Returns the sums and starts them again */
struct pocketjs_turn_stats pocketjs_turn_stats_take(void);

#endif

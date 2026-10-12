/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The start of the player is a list of steps that the modules register themselves, grouped in
 * stages. main() only runs the stages, so a module that is not built leaves no trace in it.
 */

#ifndef NEKOBOX_CORE_APP_H_
#define NEKOBOX_CORE_APP_H_

#include <stdbool.h>
#include <zephyr/sys/iterable_sections.h>

enum nekobox_stage {
	NEKOBOX_STAGE_EARLY,	/**< before anything is needed: CPU clock, checks, the display thread */
	NEKOBOX_STAGE_STORAGE,	/**< the card is mounted, the settings are read */
	NEKOBOX_STAGE_UI,		/**< the display is up: LVGL, drawing accelerators, the screens */
	NEKOBOX_STAGE_SERVICES,	/**< background services: the network */
	NEKOBOX_STAGE_READY,	/**< the first picture is on the screen */
	NEKOBOX_STAGE_COUNT,
};

struct nekobox_init {
	enum nekobox_stage stage;
	int order;		/**< steps of a stage run by ascending order */
	const char *name;	/**< printed with the time since the start of the system */
	int (*fn)(void);	/**< 0 on success; a failure in the UI stage ends the start */
};

/** Registers a start step: NEKOBOX_INIT(NEKOBOX_STAGE_STORAGE, 10, "settings", settings_init) */
#define NEKOBOX_INIT(_stage, _order, _name, _fn)                                                     \
	static const STRUCT_SECTION_ITERABLE(nekobox_init, nekobox_init_##_fn) = {                       \
		.stage = (_stage), .order = (_order), .name = (_name), .fn = (_fn),              \
	}

/** Runs the steps of a stage; returns the first error (the rest of the stage is not run) */
int nekobox_run_stage(enum nekobox_stage stage);

/*
 * Background activity (the network, Bluetooth, the scan of the SD card) is held back while a
 * game runs: the emulator needs all of the CPU. A service that starts late waits in
 * nekobox_background_wait(), a running one registers a function that is called with true when the
 * hold begins and false when it ends.
 */
void nekobox_background_hold(bool hold);
bool nekobox_background_held(void);
void nekobox_background_wait(void);
void nekobox_background_register(void (*fn)(bool hold));

#endif /* NEKOBOX_CORE_APP_H_ */

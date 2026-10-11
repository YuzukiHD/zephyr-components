/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The start of the player is a list of steps that the modules register themselves, grouped in
 * stages. main() only runs the stages, so a module that is not built leaves no trace in it.
 */

#ifndef MP5_CORE_APP_H_
#define MP5_CORE_APP_H_

#include <zephyr/sys/iterable_sections.h>

enum mp5_stage {
	MP5_STAGE_EARLY,	/**< before anything is needed: CPU clock, checks, the display thread */
	MP5_STAGE_STORAGE,	/**< the card is mounted, the settings are read */
	MP5_STAGE_UI,		/**< the display is up: LVGL, drawing accelerators, the screens */
	MP5_STAGE_SERVICES,	/**< background services: the network */
	MP5_STAGE_READY,	/**< the first picture is on the screen */
	MP5_STAGE_COUNT,
};

struct mp5_init {
	enum mp5_stage stage;
	int order;		/**< steps of a stage run by ascending order */
	const char *name;	/**< printed with the time since the start of the system */
	int (*fn)(void);	/**< 0 on success; a failure in the UI stage ends the start */
};

/** Registers a start step: MP5_INIT(MP5_STAGE_STORAGE, 10, "settings", settings_init) */
#define MP5_INIT(_stage, _order, _name, _fn)                                                     \
	static const STRUCT_SECTION_ITERABLE(mp5_init, mp5_init_##_fn) = {                       \
		.stage = (_stage), .order = (_order), .name = (_name), .fn = (_fn),              \
	}

/** Runs the steps of a stage; returns the first error (the rest of the stage is not run) */
int mp5_run_stage(enum mp5_stage stage);

#endif /* MP5_CORE_APP_H_ */

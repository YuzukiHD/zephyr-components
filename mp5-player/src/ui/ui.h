/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The screen manager. Screens register with MP5_SCREEN (ui/registry.h) and are found by name. */

#ifndef MP5_UI_UI_H_
#define MP5_UI_UI_H_

#include <lvgl.h>

#include "ui/registry.h"
#include "ui/theme.h"
#include "ui/widgets.h"

/** Shows another screen; an unknown name (a feature that is not built) does nothing */
void ui_goto(const char *name);
/** The same, with an argument for the enter function of the screen */
void ui_goto_arg(const char *name, void *arg);

/** Opens the file browser in @p dir (NULL: the root of the card) listing the files of @p kind */
#ifdef CONFIG_MP5_FEATURE_FILES
void ui_files_open(const char *dir, const char *kind);
#else
static inline void ui_files_open(const char *dir, const char *kind)
{
	(void)dir;
	(void)kind;
}
#endif

/** The first picture, then the panel on; and the loop of the user interface thread */
void ui_present(void);
void ui_loop(void);

/** Makes every screen again (the theme changed) */
void ui_rebuild_all(void);

#endif /* MP5_UI_UI_H_ */

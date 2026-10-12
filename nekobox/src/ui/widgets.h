/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Building blocks shared by the screens */

#ifndef NEKOBOX_UI_WIDGETS_H_
#define NEKOBOX_UI_WIDGETS_H_

#include <lvgl.h>
#include <stdbool.h>

/** A plain screen in the background colour; transparent for the ones that show the video */
lv_obj_t *ui_screen_new(bool transparent);

/** A round, filled button with a symbol, used by the bars of the screens */
lv_obj_t *ui_icon_button(lv_obj_t *parent, const char *symbol, int32_t size);

/** A title bar with a back button and a title, at the top of @p parent */
lv_obj_t *ui_title_bar(lv_obj_t *parent, const char *title, lv_event_cb_t on_back);

/** The dialog that sets the time of day; @p done (may be NULL) is called after Set */
void ui_set_time_dialog(void (*done)(void));

/**
 * Shows or hides the user interface plane over the video. With nothing to draw the plane is
 * switched off, which gives its memory bandwidth back to the decoder.
 */
void ui_overlay_set(bool show);

#endif /* NEKOBOX_UI_WIDGETS_H_ */

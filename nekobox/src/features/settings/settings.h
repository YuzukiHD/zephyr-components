/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_FEATURES_SETTINGS_SETTINGS_H_
#define NEKOBOX_FEATURES_SETTINGS_SETTINGS_H_

#include <lvgl.h>

/**
 * A row of the settings: an icon, a title, the value on the right (written into @p value, may
 * be NULL) and what happens when it is tapped (@p cb may be NULL). Rows of the features are
 * made with it by the create function of their NEKOBOX_SETTINGS_ROW.
 */
lv_obj_t *settings_row_create(lv_obj_t *parent, const char *symbol, const char *title,
			      lv_obj_t **value, lv_event_cb_t cb);

#endif /* NEKOBOX_FEATURES_SETTINGS_SETTINGS_H_ */

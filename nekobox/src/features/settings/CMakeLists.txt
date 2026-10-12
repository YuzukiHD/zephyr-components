# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_NEKOBOX_FEATURE_SETTINGS app PRIVATE settings.c)
target_sources_ifdef(CONFIG_NEKOBOX_FEATURE_BRIGHTNESS app PRIVATE brightness_row.c)
target_sources_ifdef(CONFIG_NEKOBOX_FEATURE_THEME_PICKER app PRIVATE theme_rows.c)

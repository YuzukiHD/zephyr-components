# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_NEKOBOX_FEATURE_DLNA app PRIVATE cast.c fetch.c settings_row.c ui_cast.c)

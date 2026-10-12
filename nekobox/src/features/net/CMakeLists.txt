# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_MP5_FEATURE_NET app PRIVATE net.c wifi_ui.c)
target_sources_ifdef(CONFIG_MP5_FEATURE_BT app PRIVATE bt.c)
if(CONFIG_MP5_FEATURE_SETTINGS)
  target_sources_ifdef(CONFIG_MP5_FEATURE_NET app PRIVATE settings_rows.c)
  target_sources_ifdef(CONFIG_MP5_FEATURE_BT app PRIVATE bt_row.c)
endif()

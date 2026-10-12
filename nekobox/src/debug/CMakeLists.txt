# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_NEKOBOX_DEBUG app PRIVATE stats.c)
if(CONFIG_NEKOBOX_SELFTEST OR CONFIG_NEKOBOX_THEME_TEST)
  target_sources(app PRIVATE selftest.c)
endif()
target_sources_ifdef(CONFIG_NEKOBOX_PROFILE app PRIVATE profile.c)

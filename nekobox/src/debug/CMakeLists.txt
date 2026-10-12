# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_MP5_DEBUG app PRIVATE stats.c)
if(CONFIG_MP5_SELFTEST OR CONFIG_MP5_THEME_TEST)
  target_sources(app PRIVATE selftest.c)
endif()
target_sources_ifdef(CONFIG_MP5_PROFILE app PRIVATE profile.c)

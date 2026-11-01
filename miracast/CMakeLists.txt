# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MIRACAST)
  zephyr_include_directories(include)
  zephyr_library()
  zephyr_library_sources(src/session.c src/media.c src/receiver.c)
  zephyr_library_sources_ifdef(CONFIG_MIRACAST_PLAYER src/player.c)
endif()

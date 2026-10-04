# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# the mGBA module is two directories up
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gba_player)

target_sources(app PRIVATE src/main.c src/video.c src/audio.c src/pad.c)
target_include_directories(app PRIVATE src)

if(CONFIG_SAMPLE_GBA_PROFILE)
  target_sources(app PRIVATE src/profile.c)
endif()

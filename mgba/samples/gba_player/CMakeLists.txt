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

if(CONFIG_SAMPLE_GBA_HOT_RAM)
  # the files of the interpreter, the memory map, the video and audio emulation that run every
  # frame; relocating a whole library would keep every function of it (no garbage collection)
  set(MGBA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
  set(S ${MGBA_ROOT}/mgba/src)
  zephyr_code_relocate(FILES
    ${S}/arm/arm.c ${S}/arm/isa-arm.c ${S}/arm/isa-thumb.c
    ${S}/gba/gba.c ${S}/gba/dma.c ${S}/gba/io.c ${S}/gba/timer.c ${S}/gba/audio.c ${S}/gba/video.c
    ${S}/gba/renderers/video-software.c ${S}/gba/renderers/software-bg.c
    ${S}/gba/renderers/software-obj.c ${S}/gba/renderers/common.c
    ${MGBA_ROOT}/src/gba_memory.c ${MGBA_ROOT}/src/renderers/software-mode0.c
    ${MGBA_ROOT}/src/gba_player.c ${S}/core/timing.c
    LOCATION RAM_TEXT)
  zephyr_code_relocate(LIBRARY app LOCATION RAM_TEXT)
endif()

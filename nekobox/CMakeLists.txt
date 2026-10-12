# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# the emulator modules sit next to this application
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../mgba ${CMAKE_CURRENT_SOURCE_DIR}/../gbsp ${CMAKE_CURRENT_SOURCE_DIR}/../nes
	${CMAKE_CURRENT_SOURCE_DIR}/../aic8800)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nekobox)

# sources include each other as "core/cfg.h"; LVGL includes lv_rvv.h from its blend sources
zephyr_include_directories(src src/gfx)

# the start steps and the registries of the user interface are read only records that the
# modules register; the linker collects them
zephyr_linker_sources(SECTIONS src/core/sections-rom.ld)
zephyr_linker_sources(SECTIONS src/ui/sections-rom.ld)

target_sources(app PRIVATE src/main.c)
add_subdirectory(src/core)
add_subdirectory(src/ui)
add_subdirectory(src/gfx)
add_subdirectory(src/debug)
add_subdirectory(src/features/home)
add_subdirectory(src/features/files)
add_subdirectory(src/features/settings)
add_subdirectory(src/features/video)
add_subdirectory(src/features/photos)
add_subdirectory(src/features/calculator)
add_subdirectory(src/features/games)
add_subdirectory(src/features/usbdisplay)
add_subdirectory(src/features/net)

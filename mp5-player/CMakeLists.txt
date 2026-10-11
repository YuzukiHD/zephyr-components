# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# the emulator modules sit next to this application
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../mgba ${CMAKE_CURRENT_SOURCE_DIR}/../nes
	${CMAKE_CURRENT_SOURCE_DIR}/../aic8800)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mp5_player)

# LVGL includes lv_rvv.h from its blend sources
zephyr_include_directories(src src/gfx src/core src/ui
	src/features/video src/features/usbdisplay src/features/files src/features/net)

target_sources(app PRIVATE
	src/main.c
	src/core/app.c
	src/core/storage.c
	src/core/boot_display.c
	src/core/clock.c
	src/core/cpu.c
	src/core/cfg.c
	src/gfx/lv_rvv.c
	src/ui/ui.c
	src/features/home/home.c
	src/features/files/files.c
	src/features/settings/settings.c
	src/features/net/net.c
	src/features/net/wifi_ui.c
	src/features/games/game.c
	src/features/photos/photos.c
	src/features/usbdisplay/usb_display.c
	src/features/usbdisplay/usb_touch.c
	src/debug/stats.c
	src/features/video/recent.c
	src/features/video/history.c
	src/features/video/ui_player.c
	src/features/video/player.c
)

if(CONFIG_SAMPLE_MP5_HOT_RAM)
  # only the files that run for every picture; relocating a whole library keeps every function
  set(V ${ZEPHYR_BASE}/drivers/vdec/sunxi)
  zephyr_code_relocate(FILES
    ${V}/vdec_sunxi.c ${V}/ve_hw.c ${V}/ve_mem.c ${V}/ve_pool.c ${V}/ve_session.c ${V}/ve_stream.c
    ${V}/ve_h264.c ${V}/ve_h264_parse.c ${V}/ve_h264_dpb.c ${V}/ve_h264_hw.c
    LOCATION RAM_TEXT)
  zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/lv_rvv.c LOCATION RAM_TEXT)
  zephyr_code_relocate(LIBRARY opencore_aac LOCATION RAM_TEXT)
endif()

# only this file uses vector instructions; the rest of the image is built as usual
set_source_files_properties(src/gfx/lv_rvv.c PROPERTIES COMPILE_OPTIONS
  "-march=rv32imafdcv_zicsr_zifencei_xtheadcmo_xtheadba_xtheadbb_xtheadbs_xtheadcondmov_xtheadmemidx_xtheadmempair")

if(CONFIG_SAMPLE_MP5_THEME_TEST OR CONFIG_SAMPLE_MP5_SELFTEST)
  target_sources(app PRIVATE src/debug/selftest.c)
endif()

# the start steps are read only records that the modules register; the linker collects them
zephyr_linker_sources(SECTIONS src/core/sections-rom.ld)

if(CONFIG_SAMPLE_MP5_G2D)
  target_sources(app PRIVATE src/gfx/lv_g2d.c)
endif()

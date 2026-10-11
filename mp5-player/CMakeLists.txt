# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# the emulator modules sit next to this application
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../mgba ${CMAKE_CURRENT_SOURCE_DIR}/../nes
	${CMAKE_CURRENT_SOURCE_DIR}/../aic8800)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mp5_player)

# LVGL includes lv_rvv.h from its blend sources
zephyr_include_directories(src)

target_sources(app PRIVATE
	src/main.c
	src/clock.c
	src/cpu.c
	src/lv_rvv.c
	src/ui.c
	src/ui_home.c
	src/ui_files.c
	src/ui_settings.c
	src/net.c
	src/ui_game.c
	src/ui_photos.c
	src/ui_usb.c
	src/ui_wifi.c
	src/config.c
	src/usb_touch.c
	src/ui_stats.c
	src/ui_recent.c
	src/history.c
	src/ui_player.c
	src/player.c
)

if(CONFIG_SAMPLE_MP5_HOT_RAM)
  # only the files that run for every picture; relocating a whole library keeps every function
  set(V ${ZEPHYR_BASE}/drivers/vdec/sunxi)
  zephyr_code_relocate(FILES
    ${V}/vdec_sunxi.c ${V}/ve_hw.c ${V}/ve_mem.c ${V}/ve_pool.c ${V}/ve_session.c ${V}/ve_stream.c
    ${V}/ve_h264.c ${V}/ve_h264_parse.c ${V}/ve_h264_dpb.c ${V}/ve_h264_hw.c
    LOCATION RAM_TEXT)
  zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/lv_rvv.c LOCATION RAM_TEXT)
  zephyr_code_relocate(LIBRARY opencore_aac LOCATION RAM_TEXT)
endif()

# only this file uses vector instructions; the rest of the image is built as usual
set_source_files_properties(src/lv_rvv.c PROPERTIES COMPILE_OPTIONS
  "-march=rv32imafdcv_zicsr_zifencei_xtheadcmo_xtheadba_xtheadbb_xtheadbs_xtheadcondmov_xtheadmemidx_xtheadmempair")

if(CONFIG_SAMPLE_MP5_THEME_TEST)
  target_sources(app PRIVATE src/ui_theme_test.c)
endif()

if(CONFIG_SAMPLE_MP5_UI_PROFILE)
  target_sources(app PRIVATE src/ui_profile.c)
endif()

if(CONFIG_SAMPLE_MP5_G2D)
  target_sources(app PRIVATE src/lv_g2d.c)
endif()

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# the WiFi driver sits next to this application
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../aic8800)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bilibili_tv)

# math.h for the TrueType rasteriser of LVGL (the minimal C library has none); the functions are in libm
zephyr_include_directories(shim)
zephyr_link_libraries(-lm)
target_compile_options(modules__lvgl PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/shim/stbtt_cfg.h)
# TinyTTF releases a kerning cache entry into the glyph cache (lv_tiny_ttf.c, ttf_get_glyph_pair_kerning_width),
# which corrupts both caches: glyphs came out overlapped and with wrong advances. Without a kerning cache
# the width is computed directly (its Kconfig symbol is a bool, it cannot be set to 0 there)
target_compile_definitions(modules__lvgl PRIVATE LV_TINY_TTF_CACHE_KERNING_CNT=0)

# sources include each other by their path below src ("net/http.h")
target_include_directories(app PRIVATE src)

# the video player (MP4 demultiplexer, H.264 and AAC decoders, display and sound) is a copy of the one of
# the NekoBox application, changed for a file that grows while it is played
file(GLOB PLAYER_SRC src/player/*.c)
target_sources(app PRIVATE ${PLAYER_SRC})

target_sources(app PRIVATE src/main.c src/core/cpu.c)
# network and file transfer, the Bilibili interface, the screens
file(GLOB NET_SRC src/net/*.c)
file(GLOB API_SRC src/api/*.c)
file(GLOB UI_SRC src/ui/*.c)
target_sources(app PRIVATE ${NET_SRC} ${API_SRC} ${UI_SRC})
target_sources_ifdef(CONFIG_BILI_SELFTEST app PRIVATE src/debug/selftest.c)

# the Chinese font (tools/make_font.py) is part of the image
generate_inc_file_for_target(app src/ui/fonts/NotoSansSC-subset.ttf ${CMAKE_CURRENT_BINARY_DIR}/font.inc)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

if(CONFIG_BILI_HOT_RAM)
  # only the files that run for every picture; relocating a whole library keeps every function
  set(V ${ZEPHYR_BASE}/drivers/vdec/sunxi)
  zephyr_code_relocate(FILES
    ${V}/vdec_sunxi.c ${V}/ve_hw.c ${V}/ve_mem.c ${V}/ve_pool.c ${V}/ve_session.c ${V}/ve_stream.c
    ${V}/ve_h264.c ${V}/ve_h264_parse.c ${V}/ve_h264_dpb.c ${V}/ve_h264_hw.c
    LOCATION RAM_TEXT)
  zephyr_code_relocate(LIBRARY opencore_aac LOCATION RAM_TEXT)
endif()

if(CONFIG_BILI_UI_RAM)
  # code of the interface runs from RAM: the flash window is not cached for data and every I-cache
  # miss of the drawing loops costs a flash read, which shows when the lists scroll
  zephyr_code_relocate(LIBRARY modules__lvgl LOCATION RAM_TEXT)
  zephyr_code_relocate(FILES ${UI_SRC} LOCATION RAM_TEXT)
endif()

target_sources_ifdef(CONFIG_BILI_CRUMBS app PRIVATE src/debug/crumbs.c)

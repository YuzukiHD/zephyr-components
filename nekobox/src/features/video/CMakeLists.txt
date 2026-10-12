# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_MP5_FEATURE_VIDEO app PRIVATE
	player.c player_audio.c player_video.c player_raw.c ui_player.c recent.c history.c)

if(CONFIG_MP5_HOT_RAM)
  # only the files that run for every picture; relocating a whole library keeps every function
  set(V ${ZEPHYR_BASE}/drivers/vdec/sunxi)
  zephyr_code_relocate(FILES
    ${V}/vdec_sunxi.c ${V}/ve_hw.c ${V}/ve_mem.c ${V}/ve_pool.c ${V}/ve_session.c ${V}/ve_stream.c
    ${V}/ve_h264.c ${V}/ve_h264_parse.c ${V}/ve_h264_dpb.c ${V}/ve_h264_hw.c
    LOCATION RAM_TEXT)
  zephyr_code_relocate(LIBRARY opencore_aac LOCATION RAM_TEXT)
endif()

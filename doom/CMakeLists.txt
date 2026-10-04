# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module for the Doom engine (doom/doomgeneric, the doomgeneric port of
# the Chocolate Doom based sources). The platform layer, the file access and the
# sound mixer are in src/.

if(NOT CONFIG_DOOM)
  return()
endif()

set(D ${CMAKE_CURRENT_LIST_DIR}/doomgeneric/doomgeneric)
if(NOT EXISTS ${D}/doomgeneric.c)
  message(FATAL_ERROR "doomgeneric sources missing, run: git submodule update --init")
endif()

zephyr_library_named(doom)

foreach(f
    dummy am_map doomdef doomstat dstrings d_event d_items d_iwad d_loop d_main
    d_mode d_net f_finale f_wipe g_game hu_lib hu_stuff info i_cdmus i_endoom
    i_joystick i_scale i_sound i_system i_timer memio m_argv m_bbox m_cheat
    m_config m_controls m_fixed m_menu m_misc m_random p_ceilng p_doors p_enemy
    p_floor p_inter p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg
    p_setup p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data r_draw
    r_main r_plane r_segs r_sky r_things sha1 sounds statdump st_lib st_stuff
    s_sound tables v_video wi_stuff w_checksum w_file w_main w_wad z_zone
    w_file_stdc i_input i_video doomgeneric)
  zephyr_library_sources(${D}/${f}.c)
endforeach()

zephyr_library_sources(src/doom_port.c src/doom_libc.c)
if(CONFIG_DOOM_SOUND)
  zephyr_library_sources(src/doom_sound.c)
  zephyr_library_compile_definitions(FEATURE_SOUND)
endif()

zephyr_library_include_directories(${D} include shim)
zephyr_include_directories(include)

zephyr_library_compile_definitions(
  CMAP256 DOOMGENERIC_RESX=320 DOOMGENERIC_RESY=200
)
zephyr_library_compile_options(-O2 -fno-strict-aliasing -w
  -march=rv32imafdc_zicsr_zifencei_xtheadcmo_xtheadba_xtheadbb_xtheadbs_xtheadcondmov_xtheadmemidx_xtheadmempair
  -include ${CMAKE_CURRENT_LIST_DIR}/shim/doom_libc.h)

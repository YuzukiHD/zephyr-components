# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_NEKOBOX_GFX_G2D app PRIVATE lv_g2d.c)

if(CONFIG_NEKOBOX_GFX_RVV)
  target_sources(app PRIVATE lv_rvv.c)
  # only this file uses vector instructions; the rest of the image is built as usual
  set_source_files_properties(lv_rvv.c PROPERTIES COMPILE_OPTIONS
    "-march=rv32imafdcv_zicsr_zifencei_xtheadcmo_xtheadba_xtheadbb_xtheadbs_xtheadcondmov_xtheadmemidx_xtheadmempair")
  if(CONFIG_NEKOBOX_HOT_RAM)
    zephyr_code_relocate(FILES ${CMAKE_CURRENT_SOURCE_DIR}/lv_rvv.c LOCATION RAM_TEXT)
  endif()
endif()

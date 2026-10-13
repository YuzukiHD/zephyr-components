# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_NEKOBOX_FEATURE_GAMES app PRIVATE game.c)

if(CONFIG_NEKOBOX_GAMES_HOT_RAM)
  # the renderer, the memory access, the frame and timer loop, the block lookup and the
  # recompiler of gpSP
  set(C ${ZEPHYR_GBSP_MODULE_DIR}/core)
  zephyr_code_relocate(FILES
    ${C}/video.cpp ${C}/cpu.cpp ${C}/cpu_threaded.c ${C}/gbsp_main.c ${C}/serial.c
    ${C}/gba_memory.c ${C}/sound.c ${C}/gba_cc_lut.c
    ${C}/riscv/riscv_emit.c ${C}/riscv/riscv_stubs.c
    LOCATION RAM_TEXT)
endif()

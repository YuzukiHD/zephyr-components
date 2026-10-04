# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module for the nofrendo NES core (nes/nofrendo, taken from the
# Retro-Go tree, GPL-2.0). The emulation core is built as it is, the ROM and
# save files go through the file system in src/nesemu.c.

if(NOT CONFIG_NES)
  return()
endif()

set(N ${CMAKE_CURRENT_LIST_DIR}/nofrendo)

zephyr_library_named(nes)

file(GLOB NES_MAPPERS ${N}/mappers/*.c)
zephyr_library_sources(
  ${N}/nes/apu.c ${N}/nes/cpu.c ${N}/nes/input.c ${N}/nes/mem.c
  ${N}/nes/mmc.c ${N}/nes/nes.c ${N}/nes/ppu.c ${N}/nes/rom.c
  ${NES_MAPPERS}
  src/nesemu.c
)

zephyr_library_compile_definitions(NES_NO_STDIO)
zephyr_library_include_directories(${N} ${N}/nes ${N}/mappers include)
zephyr_include_directories(include)

zephyr_library_compile_options(-O2 -fno-strict-aliasing -w
  -march=rv32imafdc_zicsr_zifencei_xtheadcmo_xtheadba_xtheadbb_xtheadbs_xtheadcondmov_xtheadmemidx_xtheadmempair)

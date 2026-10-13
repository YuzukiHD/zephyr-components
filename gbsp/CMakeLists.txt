# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: GPL-2.0-or-later

# Zephyr module for gpSP (core/), a Game Boy Advance emulator with a dynamic recompiler. The
# core is a copy of the libretro version of gpSP with the RISC-V back end (see README.md); the
# glue in src/ gives it the interface of the mGBA module.

if(NOT CONFIG_GBSP)
  return()
endif()

set(C ${CMAKE_CURRENT_LIST_DIR}/core)
set(GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${GEN})

# the BIOS as an array
add_custom_command(
  OUTPUT ${GEN}/bios.h
  COMMAND ${CMAKE_COMMAND} -DGBSP_BIOS_BIN=${C}/bios/open_gba_bios.bin -DGBSP_BIOS_HDR=${GEN}/bios.h
          -P ${C}/gen_bios_header.cmake
  DEPENDS ${C}/bios/open_gba_bios.bin ${C}/gen_bios_header.cmake)
add_custom_target(gbsp_bios DEPENDS ${GEN}/bios.h)

zephyr_library_named(gbsp)
add_dependencies(gbsp gbsp_bios)
zephyr_include_directories(include)

zephyr_library_sources(
  ${C}/cheats.c ${C}/cpu.cpp ${C}/gba_cc_lut.c ${C}/gba_memory.c ${C}/gbp.c
  ${C}/gpsp_memory_alloc.c ${C}/input.c ${C}/main.c ${C}/memmap.c ${C}/rfu.c ${C}/savestate.c
  ${C}/serial.c ${C}/sound.c ${C}/video.cpp
  src/gbsp_player.c)

if(CONFIG_GBSP_DYNAREC)
  zephyr_library_sources(${C}/cpu_threaded.c ${C}/riscv/riscv_emit.c ${C}/riscv/riscv_stubs.c)
  zephyr_library_compile_definitions(HAVE_DYNAREC HAVE_JIT RISCV_ARCH MMAP_JIT_CACHE)
endif()

zephyr_library_include_directories(${C} ${GEN})
zephyr_library_compile_definitions(OVERCLOCK_60FPS)
if(CONFIG_GBSP_RVV)
  zephyr_library_compile_definitions(HAVE_RVV_TILES)
endif()
if(CONFIG_GBSP_RVV)
  set(GBSP_ISA rv32imafdcv)
else()
  set(GBSP_ISA rv32imafdc)
endif()
zephyr_library_compile_options(
  ${CONFIG_GBSP_OPT_LEVEL} -fno-strict-aliasing -fomit-frame-pointer -w -msmall-data-limit=0
  -march=${GBSP_ISA}_zicsr_zifencei_xtheadcmo_xtheadba_xtheadbb_xtheadbs_xtheadcondmov_xtheadmemidx_xtheadmempair)

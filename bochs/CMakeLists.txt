# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module for the Bochs x86 emulator (bochs/Bochs, the emulator is in its
# bochs/ subdirectory). Our config.h is a configured 32 bit rv32 build: 486
# level CPU without FPU, no PCI, no plugins, no GUI, no networking.

if(NOT CONFIG_BOCHS)
  return()
endif()

set(B ${CMAKE_CURRENT_LIST_DIR}/Bochs)
set(S ${B}/bochs)
if(NOT EXISTS ${S}/cpu/cpu.cc)
  message(FATAL_ERROR "Bochs sources missing, run: git submodule update --init")
endif()

# patches to the Bochs tree (applied once)
file(GLOB patches ${CMAKE_CURRENT_LIST_DIR}/patches/*.patch)
foreach(p ${patches})
  execute_process(COMMAND git -C ${B} apply --check ${p}
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
  if(rc EQUAL 0)
    execute_process(COMMAND git -C ${B} apply ${p})
  endif()
endforeach()

zephyr_library_named(bochs)

file(GLOB cpu_src ${S}/cpu/*.cc)
file(GLOB decoder_src ${S}/cpu/decoder/*.cc)
file(GLOB fpu_src ${S}/cpu/fpu/*.cc ${S}/cpu/softfloat3e/*.cc ${S}/cpu/softfloat3e/8086-SSE/*.cc)
file(GLOB mem_src ${S}/memory/*.cc)

zephyr_library_sources(${cpu_src} ${decoder_src} ${fpu_src} ${mem_src})
if(CONFIG_DISPLAY_SUNXI)
  set(GUI_SRC src/bochs_gui.cc)
else()
  set(GUI_SRC ${S}/gui/nogui.cc)
endif()

zephyr_library_sources(
  ${S}/logio.cc ${S}/main.cc ${S}/config.cc ${S}/pc_system.cc ${S}/osdep.cc
  ${S}/crc.cc
  ${S}/gui/gui.cc ${S}/gui/keymap.cc ${GUI_SRC} ${S}/gui/paramtree.cc
  ${S}/gui/siminterface.cc ${S}/gui/textconfig.cc
  ${S}/iodev/devices.cc ${S}/iodev/virt_timer.cc ${S}/iodev/slowdown_timer.cc
  ${S}/iodev/pic.cc ${S}/iodev/pit.cc ${S}/iodev/pit82c54.cc ${S}/iodev/serial.cc
  ${S}/iodev/parallel.cc ${S}/iodev/floppy.cc ${S}/iodev/keyboard.cc
  ${S}/iodev/scancodes.cc ${S}/iodev/biosdev.cc ${S}/iodev/cmos.cc
  ${S}/iodev/harddrv.cc ${S}/iodev/dma.cc ${S}/iodev/unmapped.cc
  ${S}/iodev/extfpuirq.cc ${S}/iodev/speaker.cc
  ${S}/iodev/display/ddc.cc ${S}/iodev/display/vga.cc ${S}/iodev/display/vgacore.cc
  ${S}/iodev/hdimage/hdimage.cc ${S}/iodev/hdimage/cdrom.cc ${S}/cpu/cpudb/intel/i486dx4.cc
  src/bochs_libc.c src/bochs_setjmp.S src/bochs_stubs.cc src/bochs_roms.S ${S}/plugin.cc
)

zephyr_library_include_directories(
  include shim ${S} ${S}/cpu ${S}/instrument/stubs ${S}/iodev ${S}/gui
  ${S}/cpu/softfloat3e ${S}/cpu/softfloat3e/include ${S}/cpu/softfloat3e/8086-SSE
)
zephyr_include_directories(include)

zephyr_library_compile_definitions(
  INLINE_LEVEL=5 SOFTFLOAT_FAST_INT64 SOFTFLOAT_FAST_DIV32TO16 SOFTFLOAT_FAST_DIV64TO32
  BxICacheEntries=${CONFIG_BOCHS_ICACHE_ENTRIES}
  BX_PAGE_STAMP_ENTRIES=${CONFIG_BOCHS_PAGE_STAMP_ENTRIES}
  BxTraceCacheEntries=${CONFIG_BOCHS_TRACE_ENTRIES}
  BxTraceCacheMemPool=${CONFIG_BOCHS_TRACE_POOL})
zephyr_library_compile_options(-Os -w)
if(CONFIG_BOCHS_DIGEST)
  zephyr_library_compile_definitions(BX_DIGEST=${CONFIG_BOCHS_DIGEST_PERIOD})
  if(CONFIG_BOCHS_DIGEST_CODE)
    zephyr_library_compile_definitions(BX_DIGEST_CODE=1)
  endif()
  if(CONFIG_BOCHS_DIGEST_TO GREATER 0)
    zephyr_library_compile_definitions(BX_DIGEST_FROM=${CONFIG_BOCHS_DIGEST_FROM} BX_DIGEST_TO=${CONFIG_BOCHS_DIGEST_TO})
  endif()
endif()
target_compile_options(bochs PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-include ${CMAKE_CURRENT_LIST_DIR}/shim/bochs_libc.h>
  $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_CURRENT_LIST_DIR}/shim/bochs_libc.h>)
target_compile_options(bochs PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)


zephyr_link_libraries(-lm)

# main() of Bochs is called from the sample
set_source_files_properties(${S}/main.cc PROPERTIES COMPILE_DEFINITIONS main=bochs_main)

set_source_files_properties(src/bochs_roms.S PROPERTIES COMPILE_DEFINITIONS
  "BX_BIOS_FILE=\"${S}/bios/BIOS-bochs-legacy\";BX_VGABIOS_FILE=\"${S}/bios/VGABIOS-lgpl/VGABIOS-lgpl-latest.bin\"")

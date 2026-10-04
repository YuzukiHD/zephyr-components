# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module for the Game Boy Advance core of mGBA (the git submodule in
# mgba/). Only the emulation core is built: ARM7TDMI, GBA hardware, software
# renderer, audio, save data, and the helpers they need.

if(NOT CONFIG_MGBA)
  return()
endif()

set(MGBA_DIR ${CMAKE_CURRENT_LIST_DIR}/mgba)
if(NOT EXISTS ${MGBA_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "mGBA sources missing, run: git submodule update --init")
endif()
set(S ${MGBA_DIR}/src)

# flags.h and version.c, generated like the upstream build does
set(MGBA_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen)
file(MAKE_DIRECTORY ${MGBA_GEN}/include/mgba)
set(M_CORE_GBA ON)
set(M_CORE_GB ON)
set(MINIMAL_CORE 2)
set(COLOR_16_BIT ON)
set(COLOR_5_6_5 ON)
set(DISABLE_THREADING ON)
set(ENABLE_VFS ON)
set(ENABLE_VFS_FD ON)
configure_file(${S}/core/flags.h.in ${MGBA_GEN}/include/mgba/flags.h)
set(GIT_COMMIT "zephyr")
set(GIT_COMMIT_SHORT "zephyr")
set(GIT_BRANCH "zephyr")
set(GIT_REV 0)
set(BINARY_NAME "mgba")
set(PROJECT_NAME "mGBA")
set(VERSION_STRING "0.11-zephyr")
configure_file(${S}/core/version.c.in ${MGBA_GEN}/version.c)

zephyr_library_named(mgba)

zephyr_library_sources(
  ${S}/arm/arm.c ${S}/arm/decoder-arm.c ${S}/arm/decoder.c
  ${S}/arm/decoder-thumb.c ${S}/arm/isa-arm.c ${S}/arm/isa-thumb.c

  ${S}/gb/audio.c
  ${S}/gb/cheats.c ${S}/gb/core.c ${S}/gb/gb.c ${S}/gb/input.c ${S}/gb/io.c ${S}/gb/mbc.c ${S}/gb/mbc/huc-3.c ${S}/gb/mbc/licensed.c ${S}/gb/mbc/mbc.c ${S}/gb/mbc/pocket-cam.c ${S}/gb/mbc/tama5.c ${S}/gb/mbc/unlicensed.c ${S}/gb/memory.c ${S}/gb/overrides.c ${S}/gb/serialize.c ${S}/gb/renderers/cache-set.c ${S}/gb/renderers/software.c ${S}/gb/sio.c ${S}/gb/timer.c ${S}/gb/video.c
  ${S}/sm83/decoder.c ${S}/sm83/isa-sm83.c ${S}/sm83/sm83.c
  ${S}/gba/audio.c ${S}/gba/bios.c ${S}/gba/cart/ereader.c ${S}/gba/cart/gpio.c
  ${S}/gba/cart/matrix.c ${S}/gba/cart/unlicensed.c ${S}/gba/cart/vfame.c
  ${S}/gba/cheats.c ${S}/gba/cheats/codebreaker.c ${S}/gba/cheats/gameshark.c
  ${S}/gba/cheats/parv3.c ${S}/gba/core.c ${S}/gba/dma.c ${S}/gba/gba.c
  ${S}/gba/hle-bios.c ${S}/gba/input.c ${S}/gba/io.c src/gba_memory.c
  ${S}/gba/overrides.c ${S}/gba/renderers/cache-set.c ${S}/gba/renderers/common.c
  ${S}/gba/renderers/software-bg.c src/renderers/software-mode0.c
  ${S}/gba/renderers/software-obj.c ${S}/gba/renderers/video-software.c
  ${S}/gba/savedata.c ${S}/gba/serialize.c ${S}/gba/sharkport.c ${S}/gba/sio.c ${S}/gba/sio/gbp.c
  ${S}/gba/timer.c ${S}/gba/video.c

  ${S}/core/bitmap-cache.c ${S}/core/cache-set.c ${S}/core/cheats.c ${S}/core/config.c ${S}/core/serialize.c
  ${S}/core/core.c ${S}/core/input.c
  ${S}/core/interface.c ${S}/core/lockstep.c ${S}/core/log.c ${S}/core/map-cache.c
  ${S}/core/mem-search.c ${S}/core/rewind.c ${S}/core/sync.c
  ${S}/core/tile-cache.c ${S}/core/timing.c

  ${S}/util/circle-buffer.c ${S}/util/configuration.c ${S}/util/crc32.c
  ${S}/util/gbk-table.c ${S}/util/hash.c ${S}/util/md5.c
  ${S}/util/sha1.c ${S}/util/string.c ${S}/util/table.c ${S}/util/vector.c
  ${S}/util/vfs.c ${S}/util/audio-buffer.c ${S}/util/audio-resampler.c
  ${S}/util/geometry.c ${S}/util/image.c ${S}/util/interpolator.c
  ${S}/util/patch.c ${S}/util/patch-fast.c ${S}/util/patch-ips.c
  ${S}/util/patch-ups.c ${S}/util/ring-fifo.c ${S}/util/text-codec.c
  ${S}/util/memory.c ${S}/util/vfs/vfs-mem.c ${S}/util/vfs/vfs-fifo.c

  ${S}/third-party/inih/ini.c
  ${MGBA_GEN}/version.c

  src/gba_player.c
  src/mgba_libc.c
)

zephyr_library_include_directories(
  ${MGBA_DIR}/include
  ${S}
  ${MGBA_GEN}/include
  ${S}/third-party/inih
  include
  shim
  src/renderers
)
zephyr_include_directories(include)

zephyr_library_compile_definitions(
  MINIMAL_CORE=2 COLOR_16_BIT COLOR_5_6_5 DISABLE_THREADING ENABLE_VFS ENABLE_VFS_FD
  M_CORE_GBA M_CORE_GB MGBA_STANDALONE
  _GNU_SOURCE
)
# the C907 bit manipulation, indexed addressing and conditional move extensions
# suit an interpreter: bit field extraction, rotates and post-increment loads
if(CONFIG_MGBA_ROM_DEMAND_PAGED)
  zephyr_library_compile_definitions(MGBA_ROM_DEMAND_PAGED)
endif()
if(NOT CONFIG_MGBA_RVV)
  zephyr_library_compile_definitions(MGBA_NO_RVV)
endif()
zephyr_library_compile_options(-O2 -fno-strict-aliasing -w
  -march=rv32imafdcv_zicsr_zifencei_xtheadcmo_xtheadba_xtheadbb_xtheadbs_xtheadcondmov_xtheadmemidx_xtheadmempair
  -include ${CMAKE_CURRENT_LIST_DIR}/shim/mgba_rvv.h)

# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_MP5_FEATURE_GAMES app PRIVATE game.c)

if(CONFIG_MP5_GAMES_HOT_RAM)
  # the interpreter, the memory access, the timers and DMA, the sound and the software renderer
  set(M ${ZEPHYR_MGBA_MODULE_DIR})
  set(G ${M}/mgba/src)
  # copies with a name of their own, see the CMakeLists.txt of the module
  set(H ${CMAKE_BINARY_DIR}/modules/mgba/gen)
  zephyr_code_relocate(FILES
    ${G}/arm/arm.c ${G}/arm/isa-arm.c ${G}/arm/isa-thumb.c
    ${G}/gba/gba.c ${G}/gba/dma.c ${G}/core/timing.c
    ${H}/gba_io_hot.c ${H}/gba_timer_hot.c ${H}/gba_video_hot.c ${H}/gba_audio_hot.c
    ${H}/gb_audio_hot.c ${H}/gba_core_hot.c
    ${G}/util/audio-buffer.c ${G}/util/circle-buffer.c ${G}/gba/renderers/common.c
    ${G}/gba/renderers/software-bg.c ${G}/gba/renderers/software-obj.c
    ${G}/gba/renderers/video-software.c
    ${M}/src/gba_memory.c ${M}/src/renderers/software-mode0.c
    LOCATION RAM_TEXT)
endif()

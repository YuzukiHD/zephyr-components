cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mp4_api)

# clip.mp4 is turned into clip.inc (a comma separated list of bytes) that
# main.c includes into a const array, so the demo needs no file system
set(CLIP_INC ${ZEPHYR_BINARY_DIR}/include/generated/clip.inc)
generate_inc_file_for_target(app ${CMAKE_CURRENT_SOURCE_DIR}/clip.mp4 ${CLIP_INC})

target_sources(app PRIVATE src/main.c)

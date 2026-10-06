# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Sub build of litehtml, its bundled gumbo HTML parser and the page renderer
# (src/lh_render.cpp): liblitehtml_zephyr.a. Run by litehtml/CMakeLists.txt with a
# toolchain file that carries the architecture options of the image.
cmake_minimum_required(VERSION 3.20)
project(litehtml_zephyr LANGUAGES C CXX)

set(LH_DIR ${LITEHTML_DIR})

# the source list of the library itself, two of the files are replaced by copies
# without the standard streams
file(READ ${LH_DIR}/CMakeLists.txt _cm)
string(REGEX MATCH "set\\(SOURCE_LITEHTML([^)]*)\\)" _m "${_cm}")
string(REGEX REPLACE "[ \t\r\n]+" ";" _src "${CMAKE_MATCH_1}")
set(LH_SOURCES)
foreach(f ${_src})
  if(f STREQUAL "")
    continue()
  endif()
  if(f STREQUAL "src/url.cpp" OR f STREQUAL "src/tstring_view.cpp")
    get_filename_component(_n ${f} NAME)
    list(APPEND LH_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/${_n})
  else()
    list(APPEND LH_SOURCES ${LH_DIR}/${f})
  endif()
endforeach()

add_library(litehtml_zephyr STATIC
  ${LH_SOURCES}
  ${LH_DIR}/src/gumbo/attribute.c ${LH_DIR}/src/gumbo/char_ref.c ${LH_DIR}/src/gumbo/error.c
  ${LH_DIR}/src/gumbo/parser.c ${LH_DIR}/src/gumbo/string_buffer.c ${LH_DIR}/src/gumbo/string_piece.c
  ${LH_DIR}/src/gumbo/tag.c ${LH_DIR}/src/gumbo/tokenizer.c ${LH_DIR}/src/gumbo/utf8.c
  ${LH_DIR}/src/gumbo/util.c ${LH_DIR}/src/gumbo/vector.c
  src/lh_render.cpp src/lh_stubs.cpp src/lh_index.c)

set_target_properties(litehtml_zephyr PROPERTIES CXX_STANDARD 17 C_STANDARD 99)
target_include_directories(litehtml_zephyr PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/shim            # first: the stream headers
  ${LH_DIR}/include ${LH_DIR}/include/litehtml ${LH_DIR}/src
  ${LH_DIR}/src/gumbo/include ${LH_DIR}/src/gumbo/include/gumbo
  ${LITEHTML_API_DIR})
target_compile_options(litehtml_zephyr PRIVATE -Os -w
  $<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_CURRENT_LIST_DIR}/shim/lh_compat.h>)

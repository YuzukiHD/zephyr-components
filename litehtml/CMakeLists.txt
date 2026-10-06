# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module for litehtml (the git submodule in litehtml/).
#
# litehtml needs the C++ standard library, which does not go with the minimal C
# library of the image. It is built by its own CMake project (ext/) against the
# headers of the toolchain (header-only parts of libstdc++ only, the standard streams
# are left out), and linked into the image as an imported static library together
# with the few precompiled parts of libstdc++ (hashing, red-black trees).

if(NOT CONFIG_LITEHTML)
  return()
endif()

include(ExternalProject)

set(LITEHTML_DIR ${CMAKE_CURRENT_LIST_DIR}/litehtml)
if(NOT EXISTS ${LITEHTML_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "litehtml sources missing, run: git submodule update --init")
endif()

set(LH_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/build)

# -mabi/-march of the image
set(LH_ARCH_FLAGS "")
foreach(f ${TOOLCHAIN_C_FLAGS})
  if(f MATCHES "^-m(abi|arch)=")
    string(APPEND LH_ARCH_FLAGS " ${f}")
  endif()
endforeach()
string(STRIP "${LH_ARCH_FLAGS}" LH_ARCH_FLAGS)

string(REGEX REPLACE "gcc$" "g++" LH_CXX ${CMAKE_C_COMPILER})
set(LH_CC ${CMAKE_C_COMPILER})
set(LH_AR ${CMAKE_AR})
set(LH_RANLIB ${CMAKE_RANLIB})
configure_file(toolchain.cmake.in ${LH_BUILD_DIR}/toolchain.cmake @ONLY)

# the libstdc++ of the multilib that matches the image
separate_arguments(_arch_list UNIX_COMMAND "${LH_ARCH_FLAGS}")
execute_process(COMMAND ${CMAKE_C_COMPILER} ${_arch_list} -print-file-name=libstdc++.a
                OUTPUT_VARIABLE LH_LIBSTDCXX OUTPUT_STRIP_TRAILING_WHITESPACE)

ExternalProject_Add(litehtml_ext
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/ext
  BINARY_DIR ${LH_BUILD_DIR}
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS
    -DCMAKE_TOOLCHAIN_FILE=${LH_BUILD_DIR}/toolchain.cmake
    -DCMAKE_BUILD_TYPE=Release
    -DLITEHTML_DIR=${LITEHTML_DIR}
    -DLITEHTML_API_DIR=${CMAKE_CURRENT_LIST_DIR}/include
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR>
  BUILD_ALWAYS ON
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS ${LH_BUILD_DIR}/liblitehtml_zephyr.a
  USES_TERMINAL_BUILD ON
)

add_library(litehtml_imported STATIC IMPORTED GLOBAL)
set_target_properties(litehtml_imported PROPERTIES
  IMPORTED_LOCATION ${LH_BUILD_DIR}/liblitehtml_zephyr.a)
add_dependencies(litehtml_imported litehtml_ext)

zephyr_include_directories(include)

# the C library functions litehtml needs; the library is behind the glue on the link line,
# so the object is pulled in by the undefined symbols
zephyr_library_named(litehtml_glue)
zephyr_library_sources(src/litehtml_libc.c)
add_dependencies(litehtml_glue litehtml_ext)
if(CONFIG_LITEHTML_PAGED_HEAP)
  zephyr_library_sources(src/litehtml_paged_heap.c)
  zephyr_ld_options(-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
                    -Wl,--wrap=aligned_alloc -Wl,--wrap=free)
endif()
zephyr_link_libraries(litehtml_imported ${LH_LIBSTDCXX})
# strtod() and friends of the toolchain's libm/libc that the minimal library lacks come
# from libm; the math functions litehtml uses are few
zephyr_link_libraries(-lm)

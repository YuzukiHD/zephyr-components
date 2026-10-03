# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module for ncnn (the git submodule in ncnn/).
#
# ncnn is built by its own CMake project (ext/) with a toolchain file that
# carries the architecture options of the image and the headers of the
# minimal C library, then linked into the image as imported static libraries.

if(NOT CONFIG_NCNN)
  return()
endif()

include(ExternalProject)

set(NCNN_DIR ${CMAKE_CURRENT_LIST_DIR}/ncnn)
if(NOT EXISTS ${NCNN_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "ncnn sources missing, run: git submodule update --init")
endif()

set(NCNN_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/build)

# -mabi/-march of the image
set(NCNN_ARCH_FLAGS "")
foreach(f ${TOOLCHAIN_C_FLAGS})
  if(f MATCHES "^-m(abi|arch)=")
    string(APPEND NCNN_ARCH_FLAGS " ${f}")
  endif()
endforeach()
string(STRIP "${NCNN_ARCH_FLAGS}" NCNN_ARCH_FLAGS)

string(REGEX REPLACE "gcc$" "g++" NCNN_CXX ${CMAKE_C_COMPILER})
set(NCNN_CC ${CMAKE_C_COMPILER})
set(NCNN_AR ${CMAKE_AR})
set(NCNN_RANLIB ${CMAKE_RANLIB})
execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
                OUTPUT_VARIABLE NCNN_GCC_INCLUDE OUTPUT_STRIP_TRAILING_WHITESPACE)
set(NCNN_SHIM_DIR ${CMAKE_CURRENT_LIST_DIR}/shim)
set(NCNN_ZEPHYR_GENERATED ${PROJECT_BINARY_DIR}/include/generated)

# include directories of the kernel and the SoC (headers of the minimal C
# library include <zephyr/...>, the exception frame includes soc_context.h)
get_target_property(_zephyr_incs zephyr_interface INTERFACE_INCLUDE_DIRECTORIES)
set(NCNN_ZEPHYR_INCLUDES "")
foreach(_d ${_zephyr_incs})
  if(NOT _d MATCHES "\\$<")
    string(APPEND NCNN_ZEPHYR_INCLUDES " -I${_d}")
  endif()
endforeach()
configure_file(toolchain.cmake.in ${NCNN_BUILD_DIR}/toolchain.cmake @ONLY)

if(CONFIG_RISCV_ISA_EXT_V)
  set(NCNN_USE_RVV ON)
else()
  set(NCNN_USE_RVV OFF)
endif()

if(CONFIG_NCNN_BENCHMARK)
  set(NCNN_WITH_BENCHMARK ON)
  set(NCNN_BYPRODUCTS ${NCNN_BUILD_DIR}/libncnn_bench.a)
  set(NCNN_TARGETS ncnn ncnn_fixup ncnn_bench)
else()
  set(NCNN_WITH_BENCHMARK OFF)
  set(NCNN_BYPRODUCTS)
  set(NCNN_TARGETS ncnn ncnn_fixup)
endif()

ExternalProject_Add(ncnn_ext
  SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/ext
  BINARY_DIR ${NCNN_BUILD_DIR}
  CMAKE_GENERATOR Ninja
  CMAKE_ARGS
    -DCMAKE_TOOLCHAIN_FILE=${NCNN_BUILD_DIR}/toolchain.cmake
    -DCMAKE_BUILD_TYPE=Release
    -DNCNN_DIR=${NCNN_DIR}
    -DNCNN_LIB_DIR=${CMAKE_CURRENT_LIST_DIR}
    -DNCNN_USE_RVV=${NCNN_USE_RVV}
    -DNCNN_WITH_BENCHMARK=${NCNN_WITH_BENCHMARK}
    "-DNCNN_BENCH_MODELS=${CONFIG_NCNN_BENCHMARK_MODELS}"
    -DNCNN_PYTHON_EXE=${PYTHON_EXECUTABLE}
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target ${NCNN_TARGETS}
  BUILD_ALWAYS ON
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS ${NCNN_BUILD_DIR}/ncnn/src/libncnn.a ${NCNN_BYPRODUCTS}
  USES_TERMINAL_BUILD ON
)

add_library(ncnn_imported STATIC IMPORTED GLOBAL)
set_target_properties(ncnn_imported PROPERTIES
  IMPORTED_LOCATION ${NCNN_BUILD_DIR}/ncnn/src/libncnn.a)
add_dependencies(ncnn_imported ncnn_ext)

# the C library functions ncnn needs; the library comes after the glue on the
# link line, so the object is pulled in by an undefined symbol
zephyr_library_named(ncnn_glue)
zephyr_library_sources(ncnn_libc.c)
add_dependencies(ncnn_glue ncnn_ext)
zephyr_ld_options(-Wl,-u,sscanf)

if(CONFIG_NCNN_BENCHMARK)
  add_library(ncnn_bench_imported STATIC IMPORTED GLOBAL)
  set_target_properties(ncnn_bench_imported PROPERTIES
    IMPORTED_LOCATION ${NCNN_BUILD_DIR}/libncnn_bench.a
    INTERFACE_LINK_LIBRARIES ncnn_imported)
  add_dependencies(ncnn_bench_imported ncnn_ext)
  zephyr_link_libraries(ncnn_bench_imported)
else()
  zephyr_link_libraries(ncnn_imported)
endif()

# expf(), tanhf() and the other math functions of the toolchain's libm
zephyr_link_libraries(-lm)

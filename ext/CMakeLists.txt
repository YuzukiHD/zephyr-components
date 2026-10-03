# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Sub build of ncnn: libncnn.a and, optionally, the benchncnn program as a
# library with the entry point ncnn_bench_main().

cmake_minimum_required(VERSION 3.20)
project(ncnn_zephyr C CXX)

set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NCNN_INSTALL_SDK OFF CACHE BOOL "" FORCE)
set(NCNN_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(NCNN_PYTHON OFF CACHE BOOL "" FORCE)
set(NCNN_C_API OFF CACHE BOOL "" FORCE)
set(NCNN_PLATFORM_API OFF CACHE BOOL "" FORCE)
set(NCNN_SIMPLEOCV OFF CACHE BOOL "" FORCE)
set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
set(NCNN_THREADS OFF CACHE BOOL "" FORCE)
set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)
set(NCNN_STDIO ON CACHE BOOL "" FORCE)
set(NCNN_STRING ON CACHE BOOL "" FORCE)
set(NCNN_SIMPLESTL ON CACHE BOOL "" FORCE)
set(NCNN_SIMPLEMATH OFF CACHE BOOL "" FORCE)
# no int8 kernels at all
set(NCNN_INT8 OFF CACHE BOOL "" FORCE)
set(NCNN_RUNTIME_CPU OFF CACHE BOOL "" FORCE)
set(NCNN_ZFH OFF CACHE BOOL "" FORCE)
set(NCNN_XTHEADVECTOR OFF CACHE BOOL "" FORCE)
set(NCNN_RVV ${NCNN_USE_RVV} CACHE BOOL "" FORCE)

add_subdirectory(${NCNN_DIR} ncnn)

if(NCNN_USE_RVV)
  target_sources(ncnn PRIVATE ${CMAKE_CURRENT_LIST_DIR}/riscv_cpu.cpp)
  set(_weak_cpu 1)
else()
  set(_weak_cpu 0)
endif()

add_custom_target(ncnn_fixup ALL
  COMMAND ${CMAKE_COMMAND} -DAR=${CMAKE_AR} -DOBJCOPY=${CMAKE_OBJCOPY} -DWEAK_CPU=${_weak_cpu}
          -DARCHIVE=$<TARGET_FILE:ncnn> -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/fixup
          -P ${CMAKE_CURRENT_LIST_DIR}/weaken.cmake
  DEPENDS ncnn)

if(NCNN_WITH_BENCHMARK)
  # embed one benchmark/models/*.param as a header (the generator script
  # of ncnn_add_param() is looked up relative to the caller, so it is done here)
  macro(zephyr_ncnn_add_param PARAM_SRC)
    get_filename_component(_n ${PARAM_SRC} NAME)
    string(REPLACE ".param" "" _n "${_n}")
    string(REPLACE "." "_" _n "${_n}")
    string(REPLACE "-" "_" _n "${_n}")
    set(_hdr ${CMAKE_CURRENT_BINARY_DIR}/param/${_n}.hex.h)
    add_custom_command(
      OUTPUT ${_hdr}
      COMMAND ${CMAKE_COMMAND} -DPARAM_SRC=${PARAM_SRC} -DPARAM_SRC_NAME_WE=${_n}
              -DPARAM_HEADER=${_hdr}
              -P "${NCNN_DIR}/cmake/ncnn_generate_param_header.cmake"
      DEPENDS ${PARAM_SRC}
      VERBATIM)
    string(APPEND param_header_data "#include \"param/${_n}.hex.h\"\n")
    list(APPEND NCNN_PARAM_HEX_FILES ${_hdr})
  endmacro()

  set(_bench_src ${NCNN_DIR}/benchmark)

  file(GLOB _params ${_bench_src}/models/*.param)
  set(NCNN_PARAM_HEX_FILES)
  set(param_header_data)
  foreach(_p ${_params})
    zephyr_ncnn_add_param(${_p})
  endforeach()
  add_custom_target(ncnn-bench-params DEPENDS ${NCNN_PARAM_HEX_FILES})
  configure_file(${_bench_src}/benchncnn_param_data.h.in
                 ${CMAKE_CURRENT_BINARY_DIR}/benchncnn_param_data.h)

  # run only the selected networks
  execute_process(COMMAND ${NCNN_PYTHON_EXE} ${NCNN_LIB_DIR}/select_bench_models.py
                  ${_bench_src}/benchncnn.cpp
                  ${CMAKE_CURRENT_BINARY_DIR}/benchncnn_selected.cpp
                  "${NCNN_BENCH_MODELS}"
                  ${_bench_src}/models
                  ${NCNN_BENCH_MAX_WEIGHTS_MB}
                  COMMAND_ERROR_IS_FATAL ANY)

  add_library(ncnn_bench STATIC ${CMAKE_CURRENT_BINARY_DIR}/benchncnn_selected.cpp
                                ${CMAKE_CURRENT_LIST_DIR}/bench_entry.cpp)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/benchncnn_selected.cpp
                              PROPERTIES COMPILE_DEFINITIONS main=ncnn_bench_main_cpp)
  target_include_directories(ncnn_bench PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${_bench_src})
  add_dependencies(ncnn_bench ncnn-bench-params)
  target_link_libraries(ncnn_bench PUBLIC ncnn)
endif()

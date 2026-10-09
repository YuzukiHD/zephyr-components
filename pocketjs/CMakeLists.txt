# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module for PocketJS (the git submodule in pocketjs/, hosts/esp-idf):
# package parser, QuickJS-ng guest (submodule quickjs-ng/), UI core and RGB565
# renderer. The two Rust archives are built with cargo for the image's rv32imac.
# The C sources are compiled from the submodule unchanged; shim/ stands in for
# the few ESP-IDF headers they include.

if(NOT CONFIG_POCKETJS)
  return()
endif()

set(PJS_DIR ${CMAKE_CURRENT_LIST_DIR}/pocketjs)
set(QJS_DIR ${CMAKE_CURRENT_LIST_DIR}/quickjs-ng)
if(NOT EXISTS ${PJS_DIR}/engine/core/Cargo.toml OR NOT EXISTS ${QJS_DIR}/quickjs.c)
  message(FATAL_ERROR "PocketJS sources missing, run: git submodule update --init")
endif()
set(HOST ${PJS_DIR}/hosts/esp-idf)
set(COMP ${HOST}/components)

# --- QuickJS-ng with the immutable ArrayBuffer patch -----------------------
find_package(Python3 REQUIRED)
set(QJS_PREPARED ${CMAKE_CURRENT_BINARY_DIR}/quickjs/quickjs.c)
set(ENV{PYTHONDONTWRITEBYTECODE} 1)
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/prepare_quickjs.py
          ${QJS_DIR}/quickjs.c ${QJS_PREPARED}
          ${COMP}/pocketjs_guest/tools/prepare_quickjs.py
  RESULT_VARIABLE _prep)
if(NOT _prep EQUAL 0)
  message(FATAL_ERROR "QuickJS immutable-buffer preparation failed")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${QJS_DIR}/quickjs.c ${CMAKE_CURRENT_LIST_DIR}/tools/prepare_quickjs.py)

zephyr_library_named(pocketjs)

zephyr_library_sources(
  ${QJS_PREPARED} ${QJS_DIR}/libregexp.c ${QJS_DIR}/libunicode.c ${QJS_DIR}/dtoa.c
  ${CMAKE_CURRENT_LIST_DIR}/src/pocketjs_std.c
  ${COMP}/pocketjs_package/src/package.c
  ${COMP}/pocketjs_guest/src/guest.c
  ${COMP}/pocketjs_ui_core/src/ui_core.c
  ${COMP}/pocketjs_ui_qjs/src/ui_qjs.c
  ${COMP}/pocketjs_render_rgb565/src/render_rgb565.c
)
zephyr_include_directories(
  ${CMAKE_CURRENT_LIST_DIR}/shim ${QJS_DIR}
  ${COMP}/pocketjs_package/include ${COMP}/pocketjs_guest/include
  ${COMP}/pocketjs_ui_core/include ${COMP}/pocketjs_ui_qjs/include
  ${COMP}/pocketjs_render_rgb565/include
)
zephyr_library_include_directories(${QJS_DIR})
# QuickJS-ng switches pthread, atomics and clock_gettime off for a few single-threaded
# libcs by the name of the platform; __DJGPP is the one of them that also times with
# gettimeofday(), which src/pocketjs_std.c provides (no SharedArrayBuffer/Atomics).
zephyr_library_compile_definitions(_GNU_SOURCE QUICKJS_NG_BUILD __DJGPP)
zephyr_library_compile_options(-O2 -std=gnu11 -include ${CMAKE_CURRENT_LIST_DIR}/shim/pocketjs_libc.h
  -Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough
  -Wno-missing-field-initializers -Wno-format -Wno-cast-function-type)
# the host's own C files are written to -Wall -Wextra clean; the format macros
# of the minimal toolchain differ, so do not turn warnings into errors here
zephyr_library_link_libraries(m)

zephyr_library_sources_ifdef(CONFIG_POCKETJS_G2D ${CMAKE_CURRENT_LIST_DIR}/src/pocketjs_g2d.c)
zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/include)

# --- Rust archives ----------------------------------------------------------
find_program(CARGO cargo HINTS $ENV{HOME}/.cargo/bin REQUIRED)
get_filename_component(_cargo_dir ${CARGO} DIRECTORY)
find_program(RUSTC rustc HINTS ${_cargo_dir} NO_DEFAULT_PATH REQUIRED)
set(RUST_TARGET riscv32imac-unknown-none-elf)
set(RUST_TARGET_ARG ${RUST_TARGET})
set(CARGO_BUILD_STD "")
set(CARGO_TOOLCHAIN "")
if(CONFIG_FPU)
  # The image uses the double precision FPU (ilp32d): the Rust code is compiled for it as
  # well (a custom target, so the standard library is built with it; needs the nightly
  # toolchain with rust-src). Without it every f32 operation of the renderer is a call.
  if(CONFIG_POCKETJS_RUST_RVV)
    set(RUST_TARGET riscv32imafdcv-zephyr)
  else()
    set(RUST_TARGET riscv32imafdc-zephyr)
  endif()
  set(RUST_TARGET_ARG ${CMAKE_CURRENT_LIST_DIR}/tools/${RUST_TARGET}.json)
  set(CARGO_TOOLCHAIN +nightly-2026-07-02)
  set(CARGO_BUILD_STD -Zbuild-std=core,alloc -Zbuild-std-features=compiler-builtins-mem
                      -Zjson-target-spec)
endif()

# The wrapper gives each crate of an archive its own symbol namespace, so two
# Rust archives can be linked into one image.
set(WRAPPER_SRC ${COMP}/pocketjs_ui_core/rustc_wrapper.rs)
set(WRAPPER ${CMAKE_CURRENT_BINARY_DIR}/rustc-wrapper)
execute_process(COMMAND ${RUSTC} --edition=2021 ${WRAPPER_SRC} -o ${WRAPPER}
                COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${WRAPPER_SRC})

set(CARGO_OFFLINE_ARG "")
if(CONFIG_POCKETJS_CARGO_OFFLINE)
  set(CARGO_OFFLINE_ARG --offline)
endif()

# The renderer is built from a patched copy of its crates (same relative layout, so the
# path dependencies keep working); the rest of the tree is used as it is.
set(RS ${CMAKE_CURRENT_BINARY_DIR}/rs)
set(RS_SRC ${HOST}/native/render-rgb565 ${HOST}/native/abi ${HOST}/native/runtime
    ${PJS_DIR}/engine/backends/rgb565 ${PJS_DIR}/engine/core)
file(GLOB_RECURSE RS_FILES CONFIGURE_DEPENDS
  ${HOST}/native/render-rgb565/*.rs ${HOST}/native/render-rgb565/Cargo.*
  ${PJS_DIR}/engine/backends/rgb565/src/*.rs ${PJS_DIR}/engine/backends/rgb565/Cargo.*
  ${CMAKE_CURRENT_LIST_DIR}/tools/patch_rgb565.py)
file(REMOVE ${RS}/engine/backends/rgb565/src/lib.rs)
foreach(d render-rgb565 abi runtime)
  file(COPY ${HOST}/native/${d} DESTINATION ${RS}/hosts/esp-idf/native
       PATTERN target EXCLUDE)
endforeach()
file(COPY ${PJS_DIR}/engine/core DESTINATION ${RS}/engine PATTERN target EXCLUDE)
file(COPY ${PJS_DIR}/engine/backends/rgb565 DESTINATION ${RS}/engine/backends
     PATTERN target EXCLUDE)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/patch_rgb565.py
                ${RS}/engine/backends/rgb565/src/lib.rs
                RESULT_VARIABLE _patch)
if(NOT _patch EQUAL 0)
  message(FATAL_ERROR "patching the RGB565 renderer failed")
endif()

function(pocketjs_rust_archive crate archive)
  set(root ${HOST}/native/${crate})
  set(src_root ${root})
  if(crate STREQUAL "render-rgb565")
    set(root ${RS}/hosts/esp-idf/native/render-rgb565)
  endif()
  set(cargo_target ${CMAKE_CURRENT_BINARY_DIR}/cargo/${crate})
  set(out ${cargo_target}/${RUST_TARGET}/release/lib${archive}.a)
  set(roots ${HOST}/native/abi ${HOST}/native/runtime ${PJS_DIR}/engine/core)
  if(crate STREQUAL "render-rgb565")
    list(APPEND roots ${PJS_DIR}/engine/backends/rgb565)
    list(APPEND roots ${CMAKE_CURRENT_LIST_DIR}/tools)
  endif()
  set(patterns ${root}/src/*.rs ${root}/Cargo.toml ${root}/Cargo.lock)
  foreach(r ${roots})
    list(APPEND patterns ${r}/src/*.rs ${r}/Cargo.toml)
  endforeach()
  file(GLOB_RECURSE sources CONFIGURE_DEPENDS ${patterns})
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -E env
      CARGO_TARGET_DIR=${cargo_target} CARGO_ENCODED_RUSTFLAGS= RUSTFLAGS=
      RUSTC_WRAPPER=${WRAPPER} POCKETJS_RUST_NAMESPACE=${archive}
      CROSS_COMPILE= CC= CFLAGS=
      CARGO_PROFILE_RELEASE_LTO=fat CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1
      ${CARGO} ${CARGO_TOOLCHAIN} build ${CARGO_BUILD_STD} --release --locked --no-default-features
        ${CARGO_OFFLINE_ARG} --target ${RUST_TARGET_ARG} --manifest-path ${root}/Cargo.toml
    DEPENDS ${sources} ${WRAPPER_SRC} ${RS}/engine/backends/rgb565/src/lib.rs
            ${CMAKE_CURRENT_LIST_DIR}/tools/patch_rgb565.py
    COMMENT "cargo build ${crate} (${RUST_TARGET})"
    USES_TERMINAL VERBATIM)
  add_custom_target(${archive}_build DEPENDS ${out})
  add_library(${archive}_native STATIC IMPORTED GLOBAL)
  set_target_properties(${archive}_native PROPERTIES IMPORTED_LOCATION ${out})
  add_dependencies(${archive}_native ${archive}_build)
  add_dependencies(pocketjs ${archive}_build)
  zephyr_library_link_libraries(${archive}_native)
endfunction()

pocketjs_rust_archive(ui-core pocketjs_idf_ui_core)
pocketjs_rust_archive(render-rgb565 pocketjs_idf_render_rgb565)
zephyr_link_libraries(-Wl,--undefined=pocketjs_idf_rust_alloc
  -Wl,--undefined=pocketjs_idf_rust_dealloc -Wl,--undefined=pocketjs_idf_rust_panic)

# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# The "f101" MicroPython module: the hardware of the F101 that has no generic
# MicroPython binding.  Pass the directory of this file as USER_C_MODULES.
add_library(usermod_f101 INTERFACE)

target_sources(usermod_f101 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/modf101.c
)

target_include_directories(usermod_f101 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_compile_definitions(usermod_f101 INTERFACE MODULE_F101_ENABLED=1)

target_link_libraries(usermod INTERFACE usermod_f101)

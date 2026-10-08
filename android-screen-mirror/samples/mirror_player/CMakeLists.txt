# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# the screen mirror module is two directories up, the WiFi module is a sibling
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..
     ${CMAKE_CURRENT_SOURCE_DIR}/../../../aic8800)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mirror_player)

target_sources(app PRIVATE src/main.c src/ui.c)
target_include_directories(app PRIVATE src)

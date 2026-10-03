# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
# the ncnn module is two directories up
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../..)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ncnn_bench)

target_sources(app PRIVATE src/main.c)

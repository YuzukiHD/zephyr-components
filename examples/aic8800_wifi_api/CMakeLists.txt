cmake_minimum_required(VERSION 3.20.0)

# AIC8800 模块不在默认模块列表里，由应用指给 CMake
# the application may be reached through a symbolic link, so resolve it before going up
get_filename_component(APP_DIR ${CMAKE_CURRENT_SOURCE_DIR} REALPATH)
get_filename_component(AIC8800_DIR ${APP_DIR}/../../aic8800 ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES ${AIC8800_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(aic8800_wifi_api)

target_sources(app PRIVATE src/main.c)

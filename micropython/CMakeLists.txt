# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Glue for building the unmodified MicroPython Zephyr port
# (micropython/ports/zephyr, a git submodule) on the F101: headers that the
# minimal C library lacks and the toolchain math library.  The port itself is
# the application, see README.md for the build command.

zephyr_include_directories(shim)

zephyr_library()
zephyr_library_sources(src/f101_libc.c)

# math functions of the toolchain's libm
zephyr_link_libraries(-lm)

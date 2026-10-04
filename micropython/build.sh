#!/bin/sh
# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0
#
# Build MicroPython for the F101 EVB:  zephyr-components/micropython/build.sh [build dir]
# Run from the workspace (after `. ./env.sh`); the default build dir is build/upy.
# Extra arguments after the build dir are passed to CMake (e.g. -DCONFIG_MICROPY_HEAP_SIZE=...).
set -e

M=$(cd "$(dirname "$0")" && pwd)
BUILD=${1:-build/upy}
[ $# -gt 0 ] && shift

# configure with the cross toolchain from CROSS_COMPILE ...
west build -b f101_evb -d "$BUILD" --cmake-only "$M/micropython/ports/zephyr" -- \
	-DZEPHYR_EXTRA_MODULES="$M" \
	-DEXTRA_CONF_FILE="$M/conf/f101.conf" \
	-DEXTRA_DTC_OVERLAY_FILE="$M/conf/f101.overlay" \
	-DUSER_C_MODULES="$M/usermod/f101/micropython.cmake" "$@"

# ... but build without it: the build also compiles mpy-cross for the host
env -u CROSS_COMPILE cmake --build "$BUILD"

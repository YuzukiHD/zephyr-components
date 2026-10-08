# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module of the Android screen mirror: the ADB client over TCP (adb.c), its RSA
# authentication (adb_auth.c) and the scrcpy session on top (scrcpy.c).

if(NOT CONFIG_MIRROR)
  return()
endif()

zephyr_library_named(android_screen_mirror)
zephyr_library_sources(src/adb.c src/adb_auth.c src/scrcpy.c src/mdns.c src/pairing.c src/tls_io.c src/ed25519_ops.c)
zephyr_library_link_libraries(mbedTLS)
zephyr_include_directories(include)

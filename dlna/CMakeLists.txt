# SPDX-License-Identifier: Apache-2.0

if(CONFIG_DLNA)
  zephyr_include_directories(include)
  zephyr_library()
  zephyr_library_include_directories(src)
  zephyr_library_sources(src/dlna.c src/ssdp.c src/http_srv.c src/upnp.c src/soap.c src/util.c)
endif()

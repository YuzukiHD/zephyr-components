# Copyright (c) 2026 Yuzuki Tsuru
# SPDX-License-Identifier: Apache-2.0

# Zephyr module for the AIC8800 WiFi chips: the FullMAC host driver, the
# supplicant and the glue to the Zephyr kernel, SDIO and network stack.

if(NOT CONFIG_AIC8800)
  return()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)

zephyr_library_named(aic8800)

set(OS zephyr)

zephyr_library_sources(
  ${AIC_COMMON_SRCS}
  ${AIC_DRV_SRCS}
  ${AIC_DRV_PRIV_SRCS}
  ${AIC_WPA_SRCS}
  host/net_al/${OS}/net_al.c
  host/net_al/${OS}/net_al_wifi.c
  src/aic_zephyr_if.c
  src/aic_sockets.c
  host/platform/${OS}/porting.c
  host/platform/${OS}/sdio_port.c
  host/platform/${OS}/plat_config.c
  host/rtos_al/${OS}/rtos_al.c
  src/aic_libc.c
  wifi.c
  wifi_monitor.c
  userconfig.c
  powerlimit.c
)
if(CONFIG_AIC8800_P2P)
  zephyr_library_sources(${AIC_P2P_SRCS})
endif()
if(CONFIG_AIC8800_WPA3)
  zephyr_library_sources(${AIC_WPA3_SRCS})
  zephyr_library_compile_definitions(CONFIG_MBEDTLS)
  zephyr_library_link_libraries(mbedTLS)
endif()

zephyr_library_include_directories(
  host/inc/priv
  host/common/api
  host/drv/macif
  host/drv/sdio
  host/drv/fw
  host/drv/wlan
  host/drv/fhost
  host/drv/aic_bsp
  .
  inc
  host/rtos_al/${OS}
  host/net_al/${OS}
  host/platform/${OS}
  host/wpa_supplicant/src
  host/wpa_supplicant/src/utils
  host/wpa_supplicant/src/rsn_supp
  host/wpa_supplicant/wpa_supplicant
  host/lbh_release/lbh_lib
  compat
  src
)
zephyr_include_directories(include)

zephyr_library_compile_definitions(
  PLATFORM_ALLWIN_FREERTOS
  CONFIG_RWNX CONFIG_RWNX_FULLMAC
  USE_5G CONFIG_PMIC_SETTING CONFIG_DOWNLOAD_FW CONFIG_LOAD_USERCONFIG
  CONFIG_OFFCHANNEL CONFIG_RWNX_DBG AIC_LOG_DEBUG_ON
  CONFIG_FHOST_TX_SCHEDULE_SEPERATE CONFIG_FHOST_TX_AC_SCHEDULE
  CONFIG_TX_NOCOPY OS_NO_C_LIB_DEFINES
)
if(CONFIG_AIC8800_INTF_SDIO)
  zephyr_library_compile_definitions(CONFIG_SDIO_SUPPORT)
endif()
if(CONFIG_AIC8800_P2P)
  zephyr_library_compile_definitions(CONFIG_P2P CONFIG_INTERWORKING CONFIG_GAS CONFIG_HS20)
endif()
if(CONFIG_AIC8800_WIFI_MODE_RFTEST)
  zephyr_library_compile_definitions(CONFIG_WIFI_MODE_RFTEST)
endif()

zephyr_library_compile_options(-w -fno-strict-aliasing -include ctype.h
  -Wno-error=incompatible-pointer-types -Wno-error=int-conversion
  -Wno-error=implicit-function-declaration)

OS = aw_freertos

# LwIP version selection
ifeq ($(CONFIG_COMPONENTS_LWIP_2_1_2),y)
  LWIP = lwip-2.1.2
else ifeq ($(CONFIG_COMPONENTS_LWIP_2_0_3),y)
  LWIP = lwip-2.0.3
endif
LWIP_PATH = $(BASE)/components/common/thirdparty/network/$(LWIP)

# SDMMC include paths are provided globally by the build system (package.mk)
# Only add paths not already in the global includes
subdir-ccflags-y += -I$(BASE)/drivers/rtos-hal/include/hal/sdmmc/osal/FreeRTOS

# SDMMC internal header path (needed by sdio_port.c for _sd_define.h, _sdhost.h)
subdir-ccflags-y += -I$(BASE)/drivers/rtos-hal/hal/source/sdmmc

# LwIP include paths
subdir-ccflags-y += -I$(LWIP_PATH)/src/include/
subdir-ccflags-y += -I$(LWIP_PATH)/src/include/lwip/
subdir-ccflags-y += -I$(LWIP_PATH)/src/
subdir-ccflags-y += -I$(LWIP_PATH)/src/core/ipv4/
subdir-ccflags-y += -I$(LWIP_PATH)/src/arch/include/
subdir-ccflags-y += -I$(LWIP_PATH)/src/include/compat/posix

# AIC8800 driver include paths
AIC_DIR = components/common/thirdparty/wireless/aic8800

subdir-ccflags-y += -I$(AIC_DIR)/host/inc/priv
subdir-ccflags-y += -I$(AIC_DIR)/host/common/api
subdir-ccflags-y += -I$(AIC_DIR)/host/drv/macif
subdir-ccflags-y += -I$(AIC_DIR)/host/drv/sdio
subdir-ccflags-y += -I$(AIC_DIR)/host/drv/fw
subdir-ccflags-y += -I$(AIC_DIR)/host/drv/wlan
subdir-ccflags-y += -I$(AIC_DIR)/host/drv/fhost
subdir-ccflags-y += -I$(AIC_DIR)/host/drv/aic_bsp
subdir-ccflags-y += -I$(AIC_DIR)
subdir-ccflags-y += -I$(AIC_DIR)/inc
subdir-ccflags-y += -I$(AIC_DIR)/host/rtos_al/$(OS)
subdir-ccflags-y += -I$(AIC_DIR)/host/net_al/$(OS)
subdir-ccflags-y += -I$(AIC_DIR)/host/platform/aw_sdio
subdir-ccflags-y += -I$(AIC_DIR)/host/wpa_supplicant/src
subdir-ccflags-y += -I$(AIC_DIR)/host/wpa_supplicant/src/utils
subdir-ccflags-y += -I$(AIC_DIR)/host/wpa_supplicant/src/rsn_supp
subdir-ccflags-y += -I$(AIC_DIR)/host/wpa_supplicant/wpa_supplicant
subdir-ccflags-y += -I$(AIC_DIR)/host/lbh_release/lbh_lib

# Finsh CLI include for MSH command registration
subdir-ccflags-y += -I$(BASE)/components/thirdparty/finsh_cli

# HAL GPIO include
subdir-ccflags-y += -I$(BASE)/drivers/hal_v2/include/hal
subdir-ccflags-y += -I$(BASE)/drivers/hal_v2/hal/source/gpio

# HAL OSAL / sys_config include (for hal_cfg.h, script.h, user_gpio_set_t)
subdir-ccflags-y += -I$(BASE)/drivers/hal_v2/include/osal
subdir-ccflags-y += -I$(BASE)/include

# XRadio libc include (for lwip errno)
subdir-ccflags-y += -I$(BASE)/components/common/aw/xradio/include

# Compiler defines
EXTRA_CFLAGS += -DPLATFORM_ALLWIN_FREERTOS

# Chip interface selection (from Kconfig)
ifeq ($(CONFIG_DRIVERS_AIC_INTF_SDIO),y)
EXTRA_CFLAGS += -DCONFIG_SDIO_SUPPORT
endif
ifeq ($(CONFIG_DRIVERS_AIC_INTF_USB),y)
EXTRA_CFLAGS += -DCONFIG_USB_SUPPORT
endif

EXTRA_CFLAGS += -DCONFIG_RWNX
EXTRA_CFLAGS += -DCONFIG_RWNX_FULLMAC
EXTRA_CFLAGS += -DCONFIG_LWIP

# Chip variant selection (from Kconfig)
ifeq ($(CONFIG_DRIVERS_AIC8800D),y)
EXTRA_CFLAGS += -DCONFIG_AIC8800D
endif
ifeq ($(CONFIG_DRIVERS_AIC8800DW),y)
EXTRA_CFLAGS += -DCONFIG_AIC8800DW
endif
ifeq ($(CONFIG_DRIVERS_AIC8800D40L),y)
EXTRA_CFLAGS += -DCONFIG_AIC8800D40L
endif
ifeq ($(CONFIG_DRIVERS_AIC8800D80),y)
EXTRA_CFLAGS += -DCONFIG_AIC8800D80
endif
EXTRA_CFLAGS += -DUSE_5G
EXTRA_CFLAGS += -DCONFIG_PMIC_SETTING
EXTRA_CFLAGS += -DCONFIG_DOWNLOAD_FW
EXTRA_CFLAGS += -DCONFIG_LOAD_USERCONFIG
EXTRA_CFLAGS += -DCONFIG_OFFCHANNEL
EXTRA_CFLAGS += -DCONFIG_P2P
EXTRA_CFLAGS += -DCONFIG_RWNX_DBG
EXTRA_CFLAGS += -DAIC_LOG_DEBUG_ON
EXTRA_CFLAGS += -DCONFIG_INTERWORKING
EXTRA_CFLAGS += -DCONFIG_GAS
EXTRA_CFLAGS += -DCONFIG_HS20
EXTRA_CFLAGS += -DCONFIG_FHOST_TX_SCHEDULE_SEPERATE
EXTRA_CFLAGS += -DCONFIG_FHOST_TX_AC_SCHEDULE
EXTRA_CFLAGS += -DCONFIG_WIFI_MODE_RFTEST
EXTRA_CFLAGS += -DCONFIG_TX_NOCOPY
EXTRA_CFLAGS += -DCONFIG_AIC_SDIO_SDC_ID=2
EXTRA_CFLAGS += -DCONFIG_DRIVERS_V2_GPIO
EXTRA_CFLAGS += -DOS_NO_C_LIB_DEFINES
# mbedtls version selection
ifeq ($(CONFIG_COMPONENTS_MBEDTLS_2_16_0),y)
MBEDTLS = mbedtls-2.16.0
else ifeq ($(CONFIG_COMPONENTS_MBEDTLS_2_7_0),y)
MBEDTLS = mbedtls-2.7.0
endif
ifeq ($(CONFIG_COMPONENTS_MBEDTLS),y)
EXTRA_CFLAGS += -DCONFIG_MBEDTLS
subdir-ccflags-y += -I$(BASE)/components/common/thirdparty/network/$(MBEDTLS)/include
endif

subdir-ccflags-y += $(EXTRA_CFLAGS)

# Suppress noisy warnings that are style issues, not real bugs
EXTRA_CFLAGS += -Wno-error=unused-variable
EXTRA_CFLAGS += -Wno-error=unused-function
EXTRA_CFLAGS += -Wno-error=unused-but-set-variable
EXTRA_CFLAGS += -Wno-error=unused-label
EXTRA_CFLAGS += -Wno-error=comment
EXTRA_CFLAGS += -Wno-error=parentheses
EXTRA_CFLAGS += -Wno-error=format
EXTRA_CFLAGS += -Wno-error=format-extra-args
EXTRA_CFLAGS += -Wno-error=overflow
EXTRA_CFLAGS += -Wno-error=pointer-sign
EXTRA_CFLAGS += -Wno-error=char-subscripts
EXTRA_CFLAGS += -Wno-error=return-type
EXTRA_CFLAGS += -Wno-error=incompatible-pointer-types
EXTRA_CFLAGS += -Wno-error=address-of-packed-member
EXTRA_CFLAGS += -Wno-error=maybe-uninitialized
EXTRA_CFLAGS += -Wno-error=uninitialized
EXTRA_CFLAGS += -Wno-error=array-bounds
EXTRA_CFLAGS += -Wno-error=misleading-indentation
EXTRA_CFLAGS += -Wno-error=int-conversion
EXTRA_CFLAGS += -Wno-error=implicit-function-declaration
# -Winfinite-recursion added in GCC 4.8; skip on older toolchains
ifeq ($(shell $(CC) -Wno-error=infinite-recursion -x c -c /dev/null -o /dev/null 2>/dev/null; echo $$?),0)
EXTRA_CFLAGS += -Wno-error=infinite-recursion
endif
EXTRA_CFLAGS += -Wno-error=builtin-macro-redefined
EXTRA_CFLAGS += -Wno-error=format-overflow

# Source file lists
COMMON_OPEN_FILES := \
    host/common/src/co_list.o \
    host/common/src/co_math.o \
    host/common/src/co_pool.o \
    host/common/src/co_ring.o \
    host/common/src/co_version.o

DRV_OPEN_FILES := host/drv/fhost/fhost_config.o \
    host/drv/fhost/fhost_rx.o \
    host/drv/fhost/fhost_tx.o \
    host/drv/fhost/fhost_wpa_config.o \
    host/drv/aic_bsp/aic_bsp_driver.o \
    host/drv/aic_bsp/aic_bsp_main.o \
    host/drv/fw/aic_fw.o \
    host/drv/macif/cli_cmd.o \
    host/drv/macif/rwnx_main.o \
    host/drv/macif/rwnx_msg.o \
    host/drv/macif/rwnx_platform.o \
    host/drv/wlan/wifi_api_dummy.o

DRV_PRIV_FILES := host/drv/fhost/fhost.o \
    host/drv/fhost/fhost_ip.o \
    host/drv/fhost/fhost_ipc_cntrl.o \
    host/drv/fhost/fhost_wpa.o \
    host/drv/wlan/wlan_if.o \
    host/drv/fhost/fhost_cntrl.o \
    host/drv/macif/rwnx_msg_rx.o \
    host/drv/macif/rwnx_msg_tx.o \
    host/drv/macif/rwnx_cmds.o \
    host/drv/macif/rwnx_utils.o \
    host/drv/fhost/rwnx_radar.o

NET_AL_OPEN_FILES := host/net_al/$(OS)/net_al.o

PLATFORM_OPEN_FILES := host/platform/aw_sdio/porting.o \
    host/platform/aw_sdio/sdio_port.o \
    host/platform/aw_sdio/plat_config.o

RTOS_AL_OPEN_FILES := host/rtos_al/$(OS)/rtos_al.o

P2P_PRIV_FILES := host/wpa_supplicant/src/ap/wps_hostapd.o \
    host/wpa_supplicant/src/eapol_supp/eapol_supp_sm.o \
    host/wpa_supplicant/src/eap_peer/eap_methods.o \
    host/wpa_supplicant/src/eap_peer/eap.o \
    host/wpa_supplicant/wpa_supplicant/wps_supplicant.o \
    host/wpa_supplicant/wpa_supplicant/offchannel.o \
    host/wpa_supplicant/wpa_supplicant/p2p_supplicant.o \
    host/wpa_supplicant/wpa_supplicant/p2p_supplicant_sd.o \
    host/wpa_supplicant/wpa_supplicant/wifi_display.o


WPA_PRIV_FILES := host/wpa_supplicant/src/ap/ap_config.o \
    host/wpa_supplicant/src/ap/acs.o \
    host/wpa_supplicant/src/ap/ap_drv_ops.o \
    host/wpa_supplicant/src/ap/ap_list.o \
    host/wpa_supplicant/src/ap/ap_mlme.o \
    host/wpa_supplicant/src/ap/authsrv.o \
    host/wpa_supplicant/src/ap/beacon.o \
    host/wpa_supplicant/src/ap/bss_load.o \
    host/wpa_supplicant/src/ap/ctrl_iface_ap.o \
    host/wpa_supplicant/src/ap/dfs.o \
    host/wpa_supplicant/src/ap/drv_callbacks.o \
    host/wpa_supplicant/src/ap/eap_user_db.o \
    host/wpa_supplicant/src/ap/hostapd.o \
    host/wpa_supplicant/src/ap/hw_features.o \
    host/wpa_supplicant/src/ap/ieee802_11.o \
    host/wpa_supplicant/src/ap/ieee802_11_auth.o \
    host/wpa_supplicant/src/ap/ieee802_11_he.o \
    host/wpa_supplicant/src/ap/ieee802_11_ht.o \
    host/wpa_supplicant/src/ap/ieee802_11_shared.o \
    host/wpa_supplicant/src/ap/ieee802_11_vht.o \
    host/wpa_supplicant/src/ap/ieee802_1x.o \
    host/wpa_supplicant/src/ap/neighbor_db.o \
    host/wpa_supplicant/src/ap/pmksa_cache_auth.o \
    host/wpa_supplicant/src/ap/p2p_hostapd.o \
    host/wpa_supplicant/src/ap/rrm.o \
    host/wpa_supplicant/src/ap/sta_info.o \
    host/wpa_supplicant/src/ap/tkip_countermeasures.o \
    host/wpa_supplicant/src/ap/utils.o \
    host/wpa_supplicant/src/ap/wmm.o \
    host/wpa_supplicant/src/ap/wnm_ap.o \
    host/wpa_supplicant/src/ap/wpa_auth.o \
    host/wpa_supplicant/src/ap/wpa_auth_glue.o \
    host/wpa_supplicant/src/ap/wpa_auth_ie.o \
    host/wpa_supplicant/src/common/hw_features_common.o \
    host/wpa_supplicant/src/common/ieee802_11_common.o \
    host/wpa_supplicant/src/common/wpa_common.o \
    host/wpa_supplicant/src/common/gas.o \
    host/wpa_supplicant/src/crypto/aes-internal-dec.o \
    host/wpa_supplicant/src/crypto/aes-internal-enc.o \
    host/wpa_supplicant/src/crypto/aes-internal.o \
    host/wpa_supplicant/src/crypto/aes-cbc.o \
    host/wpa_supplicant/src/crypto/aes-omac1.o \
    host/wpa_supplicant/src/crypto/aes-unwrap.o \
    host/wpa_supplicant/src/crypto/aes-wrap.o \
    host/wpa_supplicant/src/crypto/crypto_internal-modexp.o \
    host/wpa_supplicant/src/crypto/dh_groups.o \
    host/wpa_supplicant/src/crypto/dh_group5.o \
    host/wpa_supplicant/src/crypto/md5-internal.o \
    host/wpa_supplicant/src/crypto/md5.o \
    host/wpa_supplicant/src/crypto/random.o \
    host/wpa_supplicant/src/crypto/rc4.o \
    host/wpa_supplicant/src/crypto/sha1-internal.o \
    host/wpa_supplicant/src/crypto/sha1-pbkdf2.o \
    host/wpa_supplicant/src/crypto/sha1-prf.o \
    host/wpa_supplicant/src/crypto/sha1.o \
    host/wpa_supplicant/src/crypto/sha256-internal.o \
    host/wpa_supplicant/src/crypto/sha256-prf.o \
    host/wpa_supplicant/src/crypto/sha256.o \
    host/wpa_supplicant/src/crypto/tls_none.o \
    host/wpa_supplicant/src/drivers/drivers.o \
    host/wpa_supplicant/src/drivers/driver_common.o \
    host/wpa_supplicant/src/drivers/driver_rwnx.o \
    host/wpa_supplicant/src/eapol_auth/eapol_auth_sm.o \
    host/wpa_supplicant/src/eap_common/chap.o \
    host/wpa_supplicant/src/eap_common/eap_common.o \
    host/wpa_supplicant/src/eap_common/eap_wsc_common.o \
    host/wpa_supplicant/src/eap_server/eap_server.o \
    host/wpa_supplicant/src/eap_server/eap_server_identity.o \
    host/wpa_supplicant/src/eap_server/eap_server_methods.o \
    host/wpa_supplicant/src/eap_server/eap_server_wsc.o \
    host/wpa_supplicant/src/eap_peer/eap_wsc.o \
    host/wpa_supplicant/src/l2_packet/l2_packet_rwnx.o \
    host/wpa_supplicant/src/p2p/p2p_build.o \
    host/wpa_supplicant/src/p2p/p2p.o \
    host/wpa_supplicant/src/p2p/p2p_dev_disc.o \
    host/wpa_supplicant/src/p2p/p2p_go_neg.o \
    host/wpa_supplicant/src/p2p/p2p_group.o \
    host/wpa_supplicant/src/p2p/p2p_invitation.o \
    host/wpa_supplicant/src/p2p/p2p_parse.o \
    host/wpa_supplicant/src/p2p/p2p_pd.o \
    host/wpa_supplicant/src/p2p/p2p_sd.o \
    host/wpa_supplicant/src/p2p/p2p_utils.o \
    host/wpa_supplicant/src/rsn_supp/preauth.o \
    host/wpa_supplicant/src/rsn_supp/wpa.o \
    host/wpa_supplicant/src/rsn_supp/wpa_ie.o \
    host/wpa_supplicant/src/rsn_supp/pmksa_cache.o \
    host/wpa_supplicant/src/tls/bignum.o \
    host/wpa_supplicant/src/utils/base64.o \
    host/wpa_supplicant/src/utils/bitfield.o \
    host/wpa_supplicant/src/utils/common.o \
    host/wpa_supplicant/src/utils/eloop.o \
    host/wpa_supplicant/src/utils/ip_addr.o \
    host/wpa_supplicant/src/utils/os_rwnx.o \
    host/wpa_supplicant/src/utils/wpa_byteorder_stubs.o \
    host/wpa_supplicant/src/utils/uuid.o \
    host/wpa_supplicant/src/utils/wpabuf.o \
    host/wpa_supplicant/src/utils/wpa_debug.o \
    host/wpa_supplicant/src/wps/wps.o \
    host/wpa_supplicant/src/wps/wps_attr_build.o \
    host/wpa_supplicant/src/wps/wps_attr_parse.o \
    host/wpa_supplicant/src/wps/wps_attr_process.o \
    host/wpa_supplicant/src/wps/wps_common.o \
    host/wpa_supplicant/src/wps/wps_dev_attr.o \
    host/wpa_supplicant/src/wps/wps_enrollee.o \
    host/wpa_supplicant/src/wps/wps_registrar.o \
    host/wpa_supplicant/wpa_supplicant/ap.o \
    host/wpa_supplicant/wpa_supplicant/blacklist.o \
    host/wpa_supplicant/wpa_supplicant/bss.o \
    host/wpa_supplicant/wpa_supplicant/aic8800_config.o \
    host/wpa_supplicant/wpa_supplicant/config_none.o \
    host/wpa_supplicant/wpa_supplicant/ctrl_iface.o \
    host/wpa_supplicant/wpa_supplicant/ctrl_iface_udp.o \
    host/wpa_supplicant/wpa_supplicant/eap_register.o \
    host/wpa_supplicant/wpa_supplicant/events.o \
    host/wpa_supplicant/wpa_supplicant/main_rwnx.o \
    host/wpa_supplicant/wpa_supplicant/notify.o \
    host/wpa_supplicant/wpa_supplicant/op_classes.o \
    host/wpa_supplicant/wpa_supplicant/scan.o \
    host/wpa_supplicant/wpa_supplicant/sta_rrm.o \
    host/wpa_supplicant/wpa_supplicant/wmm_ac.o \
    host/wpa_supplicant/wpa_supplicant/wpas_glue.o \
    host/wpa_supplicant/wpa_supplicant/wpa_supplicant.o \
    host/wpa_supplicant/wpa_supplicant/interworking.o \
    host/wpa_supplicant/wpa_supplicant/gas_query.o \
    host/wpa_supplicant/wpa_supplicant/hs20_supplicant.o \
    host/wpa_supplicant/src/ap/hs20.o \
    host/wpa_supplicant/src/ap/gas_serv.o

WPA3_PRIV_FILES := host/wpa_supplicant/wpa_supplicant/sme.o \
    host/wpa_supplicant/src/common/sae.o \
    host/wpa_supplicant/src/common/dragonfly.o \
    host/wpa_supplicant/src/crypto/crypto_mbedtls.o

PRIV_OBJS += $(COMMON_PRIV_FILES)
PRIV_OBJS += $(DRV_PRIV_FILES)
PRIV_OBJS += $(P2P_PRIV_FILES)
PRIV_OBJS += $(WPA_PRIV_FILES)
ifeq ($(CONFIG_COMPONENTS_MBEDTLS),y)
PRIV_OBJS += $(WPA3_PRIV_FILES)
endif

OPEN_OBJS += $(COMMON_OPEN_FILES)
OPEN_OBJS += $(DRV_OPEN_FILES)
OPEN_OBJS += $(NET_AL_OPEN_FILES)
OPEN_OBJS += $(PLATFORM_OPEN_FILES)
OPEN_OBJS += $(RTOS_AL_OPEN_FILES)

obj-$(CONFIG_DRIVERS_AIC8800) += src/aic_cmd.o \
    userconfig.o \
    wifi.o \
    wifi_monitor.o \
    powerlimit.o

obj-$(CONFIG_DRIVERS_AIC8800) += $(OPEN_OBJS)
obj-$(CONFIG_DRIVERS_AIC8800) += $(PRIV_OBJS)

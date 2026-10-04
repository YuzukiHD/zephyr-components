#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "netif/ethernet.h"

#include <sdmmc/hal_sdhost.h>
#include <sdmmc/card.h>
#include <sdmmc/sdio.h>
#include <sdmmc/sdmmc.h>
#include <sdmmc/sys/sys_debug.h>

#include "rtos_al.h"
#include "fhost_tx.h"
#include "fhost.h"
#include "cli_cmd.h"
#include "wlan_if.h"
#include "plat_config.h"
#include "porting.h"
#include "aic_log.h"
#include "wifi_driver_event.h"

extern struct rwnx_hw *g_rwnx_hw;

struct sdio_info {
    uint16_t card_id;
    SDC_InitTypeDef sdc_param;
    SDCard_InitTypeDef card_param;
    struct mmc_card *card;
};

static struct sdio_info sdio_wifi;

static int sdio_card_detect(struct sdio_info *sdio)
{
    int ret = mmc_card_create(sdio->card_id, &sdio->card_param);
    if (ret != 0) {
        printf("SDIO failed to init. ret=%d\n", ret);
        return ret;
    }

    sdio->card = mmc_card_open(sdio->card_id);
    if (sdio->card == NULL) {
        printf("card open fail\n");
        return ret;
    }
    /* scan card for detect card is exist? */
    if (!mmc_card_present(sdio->card)) {
        if (mmc_rescan(sdio->card, sdio->card->id)) {
            printf("Initial card failed!!\n");
            mmc_card_close(sdio->card_id);
            return ret;
        } else {
            printf("Initial card success\n");
            mmc_card_close(sdio->card_id);
        }
    } else {
        printf("%s not eixst\n", __func__);
        mmc_card_close(sdio->card_id);
        return ret;
    }

    return ret;
}

static void sdio_controller_init(uint32_t index)
{
    SDC_InitTypeDef sdc_param;
    struct mmc_host *host;

    memset(&sdio_wifi, 0, sizeof(struct sdio_info));
    sdio_wifi.sdc_param.cd_mode = CARD_ALWAYS_PRESENT;
    sdio_wifi.sdc_param.debug_mask = (ROM_INF_MASK | ROM_WRN_MASK | ROM_ERR_MASK | ROM_ANY_MASK);
    sdio_wifi.sdc_param.dma_use = 1;
    sdio_wifi.sdc_param.pwr_mode = POWER_MODE_330;

    sdio_wifi.card_id = index;
    sdio_wifi.card_param.debug_mask = sdio_wifi.sdc_param.debug_mask;
    sdio_wifi.card_param.type = MMC_TYPE_SDIO;

    host = hal_sdc_create(index, &sdio_wifi.sdc_param);
    hal_sdc_init(host);

    if (sdio_wifi.sdc_param.cd_mode == CARD_ALWAYS_PRESENT) {
        sdio_card_detect(&sdio_wifi);
    }
}

int aic_enable_netif(const char *ifname)
{
    if (ifname == NULL || strlen(ifname) == 0) {
        printf("Error: Invalid network interface name!\n");
        return -1;
    }

    struct netif *netif = netif_find(ifname);
    if (netif == NULL) {
        printf("Error: Cannot find netif %s!\n", ifname);
        return -1;
    }

    netif_set_up(netif);
    printf("Success: %s netif is up now!\n", ifname);
    return 0;
}

int aic_start_wl1_dhcpc(const char *ifname)
{
    struct netif *netif = NULL;
    err_t ret = ERR_OK;

    netif = netif_find(ifname);
    if (netif == NULL) {
        printf("aic_start_wl1_dhcpc: netif not found\n");
        return -1;
    }

    if (!netif_is_up(netif)) {
        netif_set_up(netif);
        printf("aic_start_wl1_dhcpc: netif is down, auto up\n");
    }

    ret = dhcp_start(netif);
    if (ret != ERR_OK) {
        printf("aic_start_wl1_dhcpc: dhcp_start failed, err=%d\n", ret);
        return ret;
    }

    printf("aic_start_wl1_dhcpc: dhcp client start success\n");
    return 0;
}

int aic_wifi_drv_event_cbk(const wifi_drv_event *drv_event)
{
    printf("wifi event: %d\n", drv_event->type);
    switch (drv_event->type) {
    case WIFI_DRV_EVENT_STA:
        aic_enable_netif("wl1");
        aic_start_wl1_dhcpc("wl1");
        break;
    default:
        break;
    }
    return 0;
}

#define CMD_BUF_SIZE 256
static char cmd_buf[CMD_BUF_SIZE];

int aic_cmd_commands(int argc, char *argv[])
{
    int i, len;
    int left = CMD_BUF_SIZE;
    char *ptr = cmd_buf;
    int offset = 1;

    if (strcmp(argv[0], "aicrf_wifi_test") == 0) {
        if (argc < 2 || strcmp(argv[1], "wlan0") != 0) {
            printf("Usage: aicrf_wifi_test wlan0 <cmd> [args...]\n");
            return -1;
        }
        offset = 2;
    }

    if (argc > offset) {
        if (strcmp(argv[offset], "init") == 0) {
            platform_config_init();
            platform_pwr_en_pin_set(0);
            rtos_task_suspend(10);
            aic_wifi_reset_power();
            sdio_controller_init(platform_get_sdc_index());
            tcpip_init(NULL, NULL);
            wifi_drv_event_set_cbk(aic_wifi_drv_event_cbk);
            aic_wifi_init(WIFI_MODE_UNKNOWN, 0, NULL);
            return 0;
        } else if (strcmp(argv[offset], "deinit") == 0) {
            return 0;
        } else if (strcmp(argv[offset], "rfinit") == 0) {
            platform_config_init();
            platform_pwr_en_pin_set(0);
            rtos_task_suspend(10);
            sdio_controller_init(platform_get_sdc_index());
            tcpip_init(NULL, NULL);
            wifi_drv_event_set_cbk(aic_wifi_drv_event_cbk);
            aic_wifi_init(WIFI_MODE_RFTEST, 0, NULL);
            return 0;
        }

        for (i = offset; i < argc && left >= 2; ++i) {
            len = snprintf(ptr, left, "%s", argv[i]);
            ptr += len;
            left -= len;
            if (i < argc - 1 && left >= 2) {
                *ptr++ = ' ';
                *ptr = '\0';
                left -= 1;
            }
        }
        aic_cli_run_cmd(cmd_buf);
    }

    return 0;
}

void aic_ifconfig(void)
{
    struct netif *netif;
    int index;

    netif = netif_list;

    while (netif != NULL) {
        printf("network interface: %c%c%s\n",
               netif->name[0],
               netif->name[1],
               (netif == netif_default) ? " (Default)" : "");
        printf("MTU: %d\n", netif->mtu);
        printf("MAC: ");
        for (index = 0; index < netif->hwaddr_len; index++)
            printf("%02x ", netif->hwaddr[index]);
        printf("\nFLAGS:");
        if (netif->flags & NETIF_FLAG_UP) printf(" UP");
        else printf(" DOWN");
        if (netif->flags & NETIF_FLAG_LINK_UP) printf(" LINK_UP");
        else printf(" LINK_DOWN");
        if (netif->flags & NETIF_FLAG_ETHARP) printf(" ETHARP");
        if (netif->flags & NETIF_FLAG_BROADCAST) printf(" BROADCAST");
        if (netif->flags & NETIF_FLAG_IGMP) printf(" IGMP");
        printf("\n");
        printf("ip address: %s\n", ipaddr_ntoa(&(netif->ip_addr)));
        printf("gw address: %s\n", ipaddr_ntoa(&(netif->gw)));
        printf("net mask  : %s\n", ipaddr_ntoa(&(netif->netmask)));
#if LWIP_IPV6
        {
            ip6_addr_t *addr;
            int addr_state;
            int i;
            addr = (ip6_addr_t *)&netif->ip6_addr[0];
            addr_state = netif->ip6_addr_state[0];
            printf("ipv6 link-local: %s state:%02X %s\n", ip6addr_ntoa(addr),
                   addr_state, ip6_addr_isvalid(addr_state) ? "VALID" : "INVALID");
            for (i = 1; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
                addr = (ip6_addr_t *)&netif->ip6_addr[i];
                addr_state = netif->ip6_addr_state[i];
                printf("ipv6[%d] address: %s state:%02X %s\n", i, ip6addr_ntoa(addr),
                       addr_state, ip6_addr_isvalid(addr_state) ? "VALID" : "INVALID");
            }
        }
#endif
        netif = netif->next;
    }

#if LWIP_DNS
    {
        const ip_addr_t *ip_addr;
        for (index = 0; index < DNS_MAX_SERVERS; index++) {
            ip_addr = dns_getserver(index);
            printf("dns server #%d: %s\n", index, ipaddr_ntoa(ip_addr));
        }
    }
#endif
}

int aic_cmd_ifconfig(int argc, char *argv[])
{
    struct netif *netif;
    ip4_addr_t addr;

    if (argc == 1) {
        aic_ifconfig();
    } else {
        if (strcmp(argv[1], "wl0") == 0) {
            printf("wlan does not support wl0, automatically switch to wl1.\n");
            strcpy(argv[1], "wl1");
        }

        netif = netif_find(argv[1]);
        if (netif == NULL)
            return 0;

        argv += 2;
        argc -= 2;
        while (argc >= 2) {
            if (strcmp(argv[0], "ip") == 0) {
                inet_pton(AF_INET, argv[1], &addr);
                netif_set_ipaddr(netif, &addr);
            } else if (strcmp(argv[0], "netmask") == 0) {
                inet_pton(AF_INET, argv[1], &addr);
                netif_set_netmask(netif, &addr);
            } else if (strcmp(argv[0], "gw") == 0) {
                inet_pton(AF_INET, argv[1], &addr);
                netif_set_gw(netif, &addr);
            } else if (strcmp(argv[0], "dns") == 0) {
                inet_pton(AF_INET, argv[1], &addr);
                dns_setserver(0, &addr);
            }
            argv += 2;
            argc -= 2;
        }

        if (argc > 0) {
            if (strcmp(argv[0], "up") == 0) {
                netif_set_up(netif);
            } else if (strcmp(argv[0], "down") == 0) {
                netif_set_down(netif);
            } else {
                printf("Usage:\n");
                printf("  ifconfig\n");
                printf("  ifconfig wl0 ip 192.168.21.1 netmask 255.255.255.0 gw 192.168.21.1 dns 8.8.8.8 up\n");
            }
        }
    }

    return 0;
}

int aic_cmd_dhcpc(int argc, char *argv[])
{
    if (argc == 2) {
        struct netif *netif;
        if (strcmp(argv[1], "wl0") == 0) {
            printf("wlan does not support wl0, automatically switch to wl1.\n");
            strcpy(argv[1], "wl1");
        }
        netif = netif_find(argv[1]);
        if (netif == NULL) {
            printf("dhcpc netif not found\n");
            return 0;
        }
        err_t ret = dhcp_start(netif);
        if (ret)
            printf("dhcp_start failed %d\n", ret);
    } else {
        printf("Usage: dhcpc if_name\n");
    }

    return 0;
}

/* Register aic command with finsh CLI */
#include <console.h>
FINSH_FUNCTION_EXPORT_ALIAS(aic_cmd_commands, aic, AIC wifi control commands);
FINSH_FUNCTION_EXPORT_ALIAS(aic_cmd_commands, aicrf_wifi_test, WiFi rf test commands);

int aic_cmd_dhcpd(int argc, char *argv[])
{
    if (argc == 2) {
        printf("aic_cmd_dhcpd: dhcpd_start(%s)\n", argv[1]);
    } else {
        printf("Usage: dhcpd if_name\n");
    }
    return 0;
}

FINSH_FUNCTION_EXPORT_ALIAS(aic_cmd_ifconfig, ifconfig, configure network interface);
FINSH_FUNCTION_EXPORT_ALIAS(aic_cmd_dhcpc, dhcpc, DHCP client);
FINSH_FUNCTION_EXPORT_ALIAS(aic_cmd_dhcpd, dhcpd, DHCP server);

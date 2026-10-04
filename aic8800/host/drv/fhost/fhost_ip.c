/**
 ****************************************************************************************
 *
 * @file fhost_ip.c
 *
 * @brief Implementation of the function related to IP configuration.
 *
 * Copyright (C) RivieraWaves 2017-2019
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @defgroup FHOST_IP FHOST_IP
 * @ingroup FHOST
 * @{
 ****************************************************************************************
 */
#include "fhost_api.h"

#if 0//def NET_AL_NO_IP

int fhost_set_vif_ip(int fvif_idx, struct fhost_vif_ip_addr_cfg *cfg)
{
    return -1;
}

int fhost_get_vif_ip(int fvif_idx, struct fhost_vif_ip_addr_cfg *cfg)
{
    return -1;
}
//#else

#include "fhost.h"

/**
 ******************************************************************************
 * @brief Stop using DHCP
 *
 * Release DHCP lease for the specified interface and stop DHCP procedure.
 *
 * @param[in] net_if  Pointer to network interface structure
 *
 * @return 0 if DHCP is stopped, != 0 an error occurred.
 ******************************************************************************
 */
static int fhost_dhcp_stop(net_if_t *net_if)
{
    // Release DHCP lease
    if (!net_dhcp_address_obtained(net_if))
    {
        if (net_dhcp_release(net_if))
        {
            aic_dbg("Failed to release DHCP\n");
            return -1;
        }

        aic_dbg("IP released\n");
    }

    // Stop DHCP
    net_dhcp_stop(net_if);

    return 0;
}

/**
 ******************************************************************************
 * @brief Retrieve IP address using DHCP
 *
 * Start DHCP procedure for the specified interface and wait until
 * it is completed using a timeout passed as parameter.
 *
 * @param[in] net_if  Pointer to network interface structure
 * @param[in] to_ms   Timeout in milliseconds
 *
 * @return 0 when ip address has been received, !=0 an error or timeout occurred.
 ******************************************************************************
 */
static int fhost_dhcp_start(net_if_t *net_if, uint32_t to_ms)
{
    uint32_t start_ms;

    // Run DHCP client
    if (net_dhcp_start(net_if))
    {
        aic_dbg("Failed to start DHCP\n");
        return -1;
    }

    start_ms = rtos_now(false);
    while ((net_dhcp_address_obtained(net_if)) &&
          (rtos_now(false) - start_ms < to_ms))
    {
        rtos_task_suspend(100);
    }

    if (net_dhcp_address_obtained(net_if))
    {
        aic_dbg("DHCP start timeout\n");
        fhost_dhcp_stop(net_if);
        return -1;
    }

    return 0;
}

/*
 ****************************************************************************************
 * PUBLIC FUNCTIONS
 ****************************************************************************************
 */
int fhost_set_vif_ip(int fvif_idx, struct fhost_vif_ip_addr_cfg *cfg)
{
    net_if_t *net_if;

    if (fvif_idx >= NX_VIRT_DEV_MAX)
        return -1;

    net_if = fhost_to_net_if(fvif_idx);
    if (!net_if)
        return -1;

    if (cfg->mode == IP_ADDR_NONE)
    {
        // clear current IP address
        fhost_dhcp_stop(net_if);
        net_if_set_ip(net_if, 0, 0, 0);
        return 0;
    }

    if (cfg->mode == IP_ADDR_STATIC_IPV4)
    {
        // To be safe
        fhost_dhcp_stop(net_if);
        net_if_set_ip(net_if, cfg->ipv4.addr, cfg->ipv4.mask, cfg->ipv4.gw);

        if (cfg->ipv4.dns)
            net_set_dns(cfg->ipv4.dns);
        else
            net_get_dns(&cfg->ipv4.dns);
    }
    else if (cfg->mode == IP_ADDR_DHCP_CLIENT)
    {
        if (fhost_dhcp_start(net_if, cfg->dhcp.to_ms))
            return -1;

        net_if_get_ip(net_if, &(cfg->ipv4.addr), &(cfg->ipv4.mask), &(cfg->ipv4.gw));
        net_get_dns(&cfg->ipv4.dns);

        aic_dbg("{FVIF-%d} ip=%d.%d.%d.%d gw=%d.%d.%d.%d\n",
                    fvif_idx,
                    cfg->ipv4.addr & 0xFF, (cfg->ipv4.addr >> 8) & 0xFF,
                    (cfg->ipv4.addr >> 16) & 0xFF, (cfg->ipv4.addr >> 24) & 0xFF,
                    cfg->ipv4.gw & 0xFF, (cfg->ipv4.gw >> 8) & 0xFF,
                    (cfg->ipv4.gw >> 16) & 0xFF, (cfg->ipv4.gw >> 24) & 0xFF);
    }
    else
    {
        return -1;
    }

    if (cfg->default_output)
         net_if_set_default(net_if);


    return 0;
}

int fhost_get_vif_ip(int fvif_idx, struct fhost_vif_ip_addr_cfg *cfg)
{
    net_if_t *net_if;

    if (fvif_idx >= NX_VIRT_DEV_MAX)
        return -1;

    net_if = fhost_to_net_if(fvif_idx);
    if (!net_if)
        return -1;

    if (!net_dhcp_address_obtained(net_if))
        cfg->mode = IP_ADDR_DHCP_CLIENT;
    else
        cfg->mode = IP_ADDR_STATIC_IPV4;

    cfg->default_output = false;

    net_if_get_ip(net_if, &(cfg->ipv4.addr), &(cfg->ipv4.mask), &(cfg->ipv4.gw));
    net_get_dns(&(cfg->ipv4.dns));

    return 0;
}
#endif // NET_AL_NO_IP
/// @}

/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

/*
 * Network abstraction layer on a Zephyr network interface: the packet buffers of the
 * driver (aic_pbuf.h) are copied from and to net_pkt in src/aic_zephyr_if.c, this file
 * keeps the transmit descriptors, the L2 (EAPOL) sockets and the interface helpers.
 */

#include <string.h>

#include "fhost_tx.h"
#include "fhost_rx.h"
#include "fhost_cntrl.h"
#include "fhost_config.h"
#include "tx_swdesc.h"
#include "rtos_al.h"
#include "net_al.h"
#include "aic_log.h"
#include "wifi.h"
#include "rwnx_utils.h"
#include "aic_zif.h"

#ifndef CONFIG_TX_NOCOPY
#error "the network layer passes packet buffers to the transmit path without a copy"
#endif

#define NX_NB_L2_FILTER 2

struct l2_filter_tag
{
    net_if_t *net_if;
    int sock;
    struct fhost_cntrl_link *link;
    uint16_t ethertype;
};

static struct l2_filter_tag l2_filter[NX_NB_L2_FILTER] = {0};
static rtos_semaphore l2_semaphore;
static rtos_mutex     l2_mutex;

struct net_tx_buf_tag
{
    /// Chained list element
    struct co_list_hdr hdr;
    TCPIP_PACKET_INFO_T pkt_hdr;
    uint8_t buf[];
};

#define NET_TXBUF_CNT 200

/// List element for the free TX buf
struct co_list net_tx_buf_free_list;
static rtos_mutex net_tx_buf_mutex;
static rtos_semaphore net_tx_buf_sema;

/*
 * PACKET BUFFERS
 ****************************************************************************************
 */

/* the block holds the buffer and, in front of the payload, room for headers */
#define PBUF_STRUCT_SIZE 64

struct pbuf *pbuf_alloc(pbuf_layer layer, uint16_t length, pbuf_type type)
{
    size_t size = PBUF_STRUCT_SIZE + PBUF_HEADROOM + ((length + 63U) & ~63U);
    struct pbuf *p;

    p = aligned_alloc(64, size);
    if (p == NULL)
        return NULL;

    memset(p, 0, sizeof(*p));
    p->payload = (uint8_t *)p + PBUF_STRUCT_SIZE + PBUF_HEADROOM;
    p->len = length;
    p->tot_len = length;
    p->ref = 1;
    p->type_internal = type;

    return p;
}

uint8_t pbuf_free(struct pbuf *p)
{
    struct pbuf *next;
    uint8_t freed = 0;

    while (p != NULL)
    {
        if (__atomic_sub_fetch(&p->ref, 1, __ATOMIC_ACQ_REL) != 0)
            break;
        next = p->next;
        free(p);
        freed++;
        p = next;
    }

    return freed;
}

void pbuf_ref(struct pbuf *p)
{
    if (p != NULL)
        __atomic_add_fetch(&p->ref, 1, __ATOMIC_RELAXED);
}

uint8_t pbuf_header(struct pbuf *p, int16_t delta)
{
    uint8_t *payload;

    if (p == NULL)
        return 1;

    payload = (uint8_t *)p->payload - delta;
    if (delta > 0)
    {
        /* not below the start of the data area */
        if (payload < (uint8_t *)p + PBUF_STRUCT_SIZE)
            return 1;
    }
    else if ((int)p->len < -delta)
    {
        return 1;
    }

    p->payload = payload;
    p->len += delta;
    p->tot_len += delta;

    return 0;
}

/*
 * FUNCTIONS
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @brief Take a free transmit descriptor
 *
 * @return Pointer to the packet info part of the descriptor, NULL if none is free
 ****************************************************************************************
 */
net_buf_tx_t *get_net_tx_buf_pkt_hdr(void)
{
    int ret = rtos_semaphore_wait(net_tx_buf_sema, 50);
    struct net_tx_buf_tag *tx_buf = NULL;

    if (ret == 0) {
        rtos_mutex_lock(net_tx_buf_mutex, -1);
        tx_buf = (struct net_tx_buf_tag *)co_list_pop_front(&net_tx_buf_free_list);
        rtos_mutex_unlock(net_tx_buf_mutex);
    }
    if (!tx_buf) {
        aic_dbg("%s get buf fail\n", __func__);
        return NULL;
    }

    return (net_buf_tx_t *)&(tx_buf->pkt_hdr);
}

/**
 ****************************************************************************************
 * @brief Entry of the Zephyr interface for the transmission of a frame
 *
 * @param[in] p_buf  Buffer holding the Ethernet frame, owned by the callee from now on
 *
 * @return 0 when the buffer was pushed for transmission by the WiFi interface
 ****************************************************************************************
 */
int aicz_net_tx(struct pbuf *p_buf)
{
    TCPIP_PACKET_INFO_T *sent_pkt;
    struct fhost_vif_tag *fhost_vif = &fhost_env.vif[0];

    if (!net_tx_buf_sema || !p_buf->tot_len) {
        pbuf_free(p_buf);
        return -1;
    }

    sent_pkt = get_net_tx_buf_pkt_hdr();
    if (!sent_pkt) {
        pbuf_free(p_buf);
        return -1;
    }
    sent_pkt->data_ptr = (uint8_t *)p_buf;
    sent_pkt->data_len = p_buf->tot_len;
    sent_pkt->net_id   = 0;
    sent_pkt->pkt_type = 0;

    fhost_tx_start(&fhost_vif->net_if, sent_pkt, NULL, NULL);

    return 0;
}

err_t net_if_init(net_if_t *net_if)
{
    struct fhost_vif_tag *vif;

    net_if->name[0] = 'w';
    net_if->name[1] = 'l';
    net_if->hwaddr_len = ETHARP_HWADDR_LEN;
    net_if->mtu = LLC_ETHER_MTU;
    net_if->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_IGMP;

    vif = &fhost_env.vif[0];
    memcpy(&vif->mac_addr, get_mac_address(), 6);
    memcpy(net_if->hwaddr, &vif->mac_addr, 6);

    return ERR_OK;
}

int net_if_add(net_if_t *net_if,
               const uint32_t *ipaddr,
               const uint32_t *netmask,
               const uint32_t *gw,
               struct fhost_vif_tag *vif)
{
    net_if->state = vif;
    net_if->zif = aicz_zif();
    if (net_if->zif == NULL)
        return -1;

    net_if_init(net_if);
    aicz_zif_set_mac(net_if->zif, net_if->hwaddr);
    if (ipaddr)
        aicz_zif_ip_set(net_if->zif, *ipaddr, netmask ? *netmask : 0, gw ? *gw : 0);

    AIC_LOG_PRINTF("net_if_add %c%c\n", net_if->name[0], net_if->name[1]);

    return 0;
}

const uint8_t *net_if_get_mac_addr(net_if_t *net_if)
{
    return (uint8_t *)net_if->hwaddr;
}

net_if_t *net_if_find_from_name(const char *name)
{
    net_if_t *net_if = &fhost_env.vif[0].net_if;

    if (net_if->state && name[0] == net_if->name[0] && name[1] == net_if->name[1])
        return net_if;

    return NULL;
}

net_if_t *net_if_find_from_wifi_idx(unsigned int idx)
{
    if ((idx >= NX_VIRT_DEV_MAX) || (fhost_env.vif[idx].mac_vif == NULL))
        return NULL;

    if (fhost_env.vif[idx].mac_vif->type != VIF_UNKNOWN)
    {
        return &fhost_env.vif[idx].net_if;
    }

    return NULL;
}

int net_if_get_name(net_if_t *net_if, char *buf, int len)
{
    if (len > 0)
        buf[0] = net_if->name[0];
    if (len > 1)
        buf[1] = net_if->name[1];
    if (len > 2)
        buf[2] = net_if->num + '0';
    if (len > 3)
        buf[3] = '\0';

    return 3;
}

int net_if_get_wifi_idx(net_if_t *net_if)
{
    struct fhost_vif_tag *vif;
    int idx;

    if (!net_if)
        return -1;

    vif = (struct fhost_vif_tag *)net_if;
    idx = CO_GET_INDEX(vif, fhost_env.vif);

    /* sanity check */
    if (&fhost_env.vif[idx].net_if == net_if)
        return idx;

    return -1;
}

void net_if_up(net_if_t *net_if)
{
    if (!net_if->zif)
        return;
    net_if->flags |= NETIF_FLAG_UP;
    aicz_zif_carrier(net_if->zif, true);
}

void net_if_down(net_if_t *net_if)
{
    if (!net_if->zif)
        return;
    net_if->flags &= ~NETIF_FLAG_UP;
    aicz_zif_carrier(net_if->zif, false);
}

void net_if_set_default(net_if_t *net_if)
{
    if (net_if && net_if->zif)
        aicz_zif_set_default(net_if->zif);
}

void net_if_set_ip(net_if_t *net_if, uint32_t ip, uint32_t mask, uint32_t gw)
{
    if (!net_if || !net_if->zif)
        return;

    aicz_zif_ip_set(net_if->zif, ip, mask, gw);
}

int net_if_get_ip(net_if_t *net_if, uint32_t *ip, uint32_t *mask, uint32_t *gw)
{
    if (!net_if || !net_if->zif)
        return -1;

    return aicz_zif_ip_get(net_if->zif, ip, mask, gw);
}

int net_if_input(net_buf_rx_t *buf, net_if_t *net_if, void *addr, uint16_t len, net_buf_free_fn free_fn)
{
    /* received data goes through rx_eth_data_process */
    return 0;
}

struct fhost_vif_tag *net_if_vif_info(net_if_t *net_if)
{
    return ((struct fhost_vif_tag *)net_if->state);
}

net_buf_tx_t *net_buf_tx_alloc(const uint8_t *payload, uint32_t length)
{
    unsigned int reserved_len = SDIO_HOSTDESC_SIZE;
    net_buf_tx_t *buf = rtos_malloc(CO_ALIGN4_HI(sizeof(net_buf_tx_t)) + CO_ALIGN4_HI(length + reserved_len));

    if (!buf) {
        AIC_LOG_ERROR("%s tx buffer null\n", __func__);
        return NULL;
    }

    memset(buf, 0, (sizeof(net_buf_tx_t)));
    uint8_t *payload_buf = (uint8_t *)buf + CO_ALIGN4_HI(sizeof(net_buf_tx_t));

    memcpy((payload_buf + reserved_len), payload, length);
    buf->data_ptr = (payload_buf + reserved_len);
    buf->data_len = length;
    buf->pkt_type = 0xFF;

    return buf;
}

void net_buf_tx_info(net_buf_tx_t *buf, uint16_t *tot_len, uint8_t *seg_cnt)
{
    *tot_len = buf->data_len;
    *seg_cnt = 1;
}

void net_buf_tx_free(net_buf_tx_t *buf)
{
    if (!buf) {
        return ;
    }

    if (0xFF == buf->pkt_type) {
        rtos_free(buf);
    } else {
        uint8_t *list_hdr = (uint8_t *)buf;

        list_hdr -= sizeof(struct co_list_hdr);

        pbuf_free((struct pbuf *)buf->data_ptr);

        rtos_mutex_lock(net_tx_buf_mutex, -1);
        co_list_push_back(&net_tx_buf_free_list, (struct co_list_hdr *)(list_hdr));
        rtos_mutex_unlock(net_tx_buf_mutex);
        rtos_semaphore_signal(net_tx_buf_sema, 0);
    }
}

uint32_t net_buf_tx_cnt(void)
{
    rtos_mutex_lock(net_tx_buf_mutex, -1);
    uint32_t cnt = co_list_cnt(&net_tx_buf_free_list);
    rtos_mutex_unlock(net_tx_buf_mutex);

    return cnt;
}

int net_init(void)
{
    uint16_t i;

    if (net_tx_buf_sema)
        return 0;

    if (rtos_semaphore_create(&l2_semaphore, "l2_semaphore", 1, 0))
    {
        ASSERT_ERR(0);
    }
    if (rtos_mutex_create(&l2_mutex, "l2_mutex"))
    {
        ASSERT_ERR(0);
    }

    // Initial free tx buf
    co_list_init(&net_tx_buf_free_list);
    if (rtos_semaphore_create(&net_tx_buf_sema, "net_tx_buf_sema", NET_TXBUF_CNT, 0)) {
        ASSERT_ERR(0);
    }
    for (i = 0; i < NET_TXBUF_CNT; i++)
    {
        struct net_tx_buf_tag *net_tx_buffer = rtos_malloc(sizeof(struct net_tx_buf_tag));
        if (net_tx_buffer) {
            co_list_push_back(&net_tx_buf_free_list, (struct co_list_hdr *)(net_tx_buffer));
            rtos_semaphore_signal(net_tx_buf_sema, 0);
        }
    }
    AIC_LOG_PRINTF("net_tx_buf_sema initial count:%d\n", rtos_semaphore_get_count(net_tx_buf_sema));
    if (rtos_mutex_create(&net_tx_buf_mutex, "net_tx_buf_mutex"))
    {
        ASSERT_ERR(0);
    }

    return 0;
}

int net_deinit(void)
{
    if (l2_semaphore) {
        rtos_semaphore_delete(l2_semaphore);
        l2_semaphore = NULL;
    }
    if (l2_mutex) {
        rtos_mutex_delete(l2_mutex);
        l2_mutex = NULL;
    }
    if (net_tx_buf_sema) {
        rtos_semaphore_delete(net_tx_buf_sema);
        net_tx_buf_sema = NULL;
    }
    if (net_tx_buf_mutex) {
        rtos_mutex_lock(net_tx_buf_mutex, -1);
        struct net_tx_buf_tag *net_tx_buffer = (struct net_tx_buf_tag *)co_list_pop_front(&net_tx_buf_free_list);
        while (net_tx_buffer) {
            rtos_free(net_tx_buffer);
            net_tx_buffer = (struct net_tx_buf_tag *)co_list_pop_front(&net_tx_buf_free_list);
        }
        rtos_mutex_unlock(net_tx_buf_mutex);
        rtos_mutex_delete(net_tx_buf_mutex);
        net_tx_buf_mutex = NULL;
    }

    return 0;
}

#ifndef CONFIG_RX_NOCOPY
int rx_eth_data_process(unsigned char *pdata, unsigned short len, net_if_t *netif)
{
    if (netif->zif)
        aicz_zif_rx(netif->zif, pdata, len);

    return 0;
}
#else /* CONFIG_RX_NOCOPY */
int rx_eth_data_process(unsigned char *pdata, unsigned short len, net_if_t *netif, bool is_pbuf)
{
    struct pbuf *p = (struct pbuf *)pdata;

    if (!is_pbuf) {
        if (netif->zif)
            aicz_zif_rx(netif->zif, pdata, len);
    } else {
        if (netif->zif)
            aicz_zif_rx(netif->zif, p->payload, len);
        pbuf_free(p);
    }

    return 0;
}

struct pbuf *rx_eth_pbuf_alloc(unsigned short len)
{
    return pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
}

void rx_eth_pbuf_free(struct pbuf *p)
{
    pbuf_free(p);
}
#endif /* CONFIG_RX_NOCOPY */

static void net_l2_send_cfm(uint32_t frame_id, bool acknowledged, void *arg)
{
    if (arg)
        *((bool *)arg) = acknowledged;

    rtos_semaphore_signal(l2_semaphore, false);
}

int net_l2_send(net_if_t *net_if, const uint8_t *data, int data_len, uint16_t ethertype,
                const uint8_t *dst_addr, bool *ack)
{
    int res;
    struct pbuf *p_buf = NULL;
    TCPIP_PACKET_INFO_T *sent_pkt;

    if (net_if == NULL || data == NULL)
        return -1;

    p_buf = pbuf_alloc(PBUF_TRANSPORT, data_len, PBUF_RAM);
    if (p_buf == NULL)
        return 0;
    memcpy(p_buf->payload, data, data_len);

    if (dst_addr)
    {
        // Need to add ethernet header as fhost_tx_start is called directly
        struct mac_eth_hdr *ethhdr;

        if (pbuf_header(p_buf, sizeof(*ethhdr))) {
            pbuf_free(p_buf);
            return -1;
        }
        ethhdr = (struct mac_eth_hdr *)p_buf->payload;
        ethhdr->type = htons(ethertype);
        memcpy(&ethhdr->da, dst_addr, 6);
        memcpy(&ethhdr->sa, net_if->hwaddr, 6);
    }

    sent_pkt = get_net_tx_buf_pkt_hdr();
    if (!sent_pkt) {
        pbuf_free(p_buf);
        return -1;
    }
    sent_pkt->data_ptr = (uint8_t *)p_buf;
    sent_pkt->data_len = p_buf->tot_len;
    sent_pkt->net_id   = 0;
    sent_pkt->pkt_type = 0;

    // Ensure no other thread will program a L2 transmission while this one is waiting
    // for its confirmation
    rtos_mutex_lock(l2_mutex, -1);

    // In order to implement this function as blocking until the completion of the frame
    // transmission, directly call fhost_tx_start with a confirmation callback.
    res = fhost_tx_start(net_if, sent_pkt, net_l2_send_cfm, ack);

    // Wait for the transmission completion
    rtos_semaphore_wait(l2_semaphore, 500);

    // Now new L2 transmissions are possible
    rtos_mutex_unlock(l2_mutex);

    return res;
}

int net_l2_socket_create(net_if_t *net_if, uint16_t ethertype)
{
    struct l2_filter_tag *filter = NULL;
    int i;
    struct fhost_cntrl_link *link;

    /* First find free filter and check that socket for this ethertype/net_if couple
       doesn't already exists */
    for (i = 0; i < NX_NB_L2_FILTER; i++)
    {
        if ((l2_filter[i].net_if == net_if) &&
            (l2_filter[i].ethertype == ethertype))
        {
            return -1;
        }
        else if ((filter == NULL) && (l2_filter[i].net_if == NULL))
        {
            filter = &l2_filter[i];
        }
    }

    if (!filter)
        return -1;

    // Open link with cntrl task to send cfgrwnx commands and retrieve events
    link = fhost_cntrl_cfgrwnx_link_open();
    if (link == NULL)
        return -1;

    filter->link = link;
    filter->sock = link->sock_recv;
    if (filter->sock == -1)
        return -1;

    filter->net_if = net_if;
    filter->ethertype = ethertype;

    return filter->sock;
}

int net_l2_socket_delete(int sock)
{
    int i;

    for (i = 0; i < NX_NB_L2_FILTER; i++)
    {
        if ((l2_filter[i].net_if != NULL) &&
            (l2_filter[i].sock == sock))
        {
            l2_filter[i].net_if = NULL;
            fhost_cntrl_cfgrwnx_link_close(l2_filter[i].link);
            l2_filter[i].sock = -1;
            return 0;
        }
    }

    return -1;
}

err_t net_eth_receive(unsigned char *pdata, unsigned short len, net_if_t *netif)
{
    struct l2_filter_tag *filter = NULL;
    struct mac_eth_hdr *ethhdr = (struct mac_eth_hdr *)pdata;
    uint16_t ethertype = ntohs(ethhdr->type);
    uint8_t *payload;
    int i;

    for (i = 0; i < NX_NB_L2_FILTER; i++)
    {
        if ((l2_filter[i].net_if == netif) &&
            (l2_filter[i].ethertype == ethertype))
        {
            filter = &l2_filter[i];
            break;
        }
    }

    if (!filter)
        return -1;

    if (send(filter->link->sock_send, pdata, len, 0) < 0)
    {
        AIC_LOG_PRINTF("Err: %s len %d\n", __func__, len);
        return -1;
    }

    payload = &pdata[sizeof(*ethhdr)];
    len -= sizeof(*ethhdr);
    extern void fhost_wpa_rx_eapol(uint8_t *src, uint16_t len, uint8_t *buf);
    fhost_wpa_rx_eapol((uint8_t *)ethhdr->sa.array, len, payload);

    return ERR_OK;
}

/*
 * DHCP: the client is the one of the Zephyr stack, it is started on the interface
 * once the link is up.
 */
static int net_dhcp_started;

int net_dhcp_start(net_if_t *net_if)
{
    if (!net_if || !net_if->zif)
        return -1;

    if (aicz_zif_dhcp_start(net_if->zif))
        return -1;
    net_dhcp_started = 1;

    return 0;
}

void net_dhcp_stop(net_if_t *net_if)
{
    if (net_if && net_if->zif)
        aicz_zif_dhcp_stop(net_if->zif);
    net_dhcp_started = 0;
}

int net_dhcp_start_status(void)
{
    return net_dhcp_started;
}

int net_dhcp_release(net_if_t *net_if)
{
    net_dhcp_stop(net_if);

    return 0;
}

int net_dhcp_address_obtained(net_if_t *net_if)
{
    if (!net_if || !net_if->zif)
        return -1;

    return aicz_zif_ip_assigned(net_if->zif);
}

/* name servers are those of the Zephyr resolver */
int net_set_dns(uint32_t dns_server)
{
    return -1;
}

int net_get_dns(uint32_t *dns_server)
{
    return -1;
}

char *aic_inet_ntoa(struct in_addr addr)
{
    return inet_ntoa(addr);
}

#ifndef ETHER_ADDR_LEN
#define ETHER_ADDR_LEN 6
#endif

struct ether_header {
    uint8_t  ether_dhost[ETHER_ADDR_LEN];
    uint8_t  ether_shost[ETHER_ADDR_LEN];
    uint16_t ether_type;
} __attribute__ ((aligned(1), packed));

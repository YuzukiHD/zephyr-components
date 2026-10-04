/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#ifdef PLATFORM_ALLWIN_RT_THREAD
#include <rtthread.h>
#include "lwip/netifapi.h"
#include "lwip/etharp.h"
#include "lwip/dns.h"
#endif

/* Forward declarations for tcpip_adapter interface */
typedef enum {
    MODE_STA = 0,
    MODE_AP,
    IF_MAX,
} if_type_t;
void set_netif(if_type_t mode, struct netif *netif);

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
#ifdef CONFIG_USB_SUPPORT
#include "usb_port.h"
#endif
#if LWIP_IPV6
#include "ethip6.h"
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

#define ERR_BUF (-1)
#define ERR_OK (0)

struct net_tx_buf_tag
{
    /// Chained list element
    struct co_list_hdr hdr;
    TCPIP_PACKET_INFO_T pkt_hdr;
    #ifdef CONFIG_TX_NOCOPY
    uint8_t buf[];
    #else
    uint8_t buf[1600];
    #endif
};

#ifdef CONFIG_TX_NOCOPY
#define NET_TXBUF_CNT 200//28
#else
#define NET_TXBUF_CNT 40//28
#endif
/// List element for the free TX buf
struct co_list net_tx_buf_free_list;
static rtos_mutex net_tx_buf_mutex;
static rtos_semaphore net_tx_buf_sema;
static bool net_inited = false;

#if defined PLATFORM_ALLWIN_RT_THREAD
static uint8_t *deliver_buf = NULL;
static uint32_t deliver_len = 0;
#endif

/*
 * FUNCTIONS
 ****************************************************************************************
 */

#if 1
#ifdef DUMP_HEX_DATA
#define DHCP_MESSAGE_TYPE_OFFSET 0x11A

static void dump_hex_data(void* ptr, int len, char* name)
{
    unsigned char *data = ((unsigned char*)ptr);
    u8_t* options = (u8_t*)ptr;
    const struct ip_hdr *iphdr;
    iphdr = (struct ip_hdr *)data;
    int i;

    //printf("\n[%s] TYPE[%02x], OP[%02x]\n", name, options[DHCP_MESSAGE_TYPE_OFFSET], options[
DHCP_MESSAGE_TYPE_OFFSET + 2]);
    printf("[Eth %s] \r\n[%04d]\n", name, len);

    for(i=0; i < len; i++)
    {
        if(!(i % 0x10))
            printf("\n");

        printf("%02x ", data[i]);
    }
    printf("\n\n");
}
#endif


/**
 ****************************************************************************************
 * @brief Callback used by the networking stack to push a buffer for transmission by the
 * WiFi interface.
 *
 * @param[in] net_if Pointer to the network interface on which the TX is done
 * @param[in] p_buf  Pointer to the buffer to transmit
 *
 * @return ERR_OK upon successful pushing of the buffer, ERR_BUF otherwise
 ****************************************************************************************
 */
 
#ifdef CONFIG_TX_NOCOPY
net_buf_tx_t *get_net_tx_buf_pkt_hdr(void)
{
    int ret = rtos_semaphore_wait(net_tx_buf_sema, 50);
    struct net_tx_buf_tag *tx_buf = NULL;
    if (ret == 0) {
        rtos_mutex_lock(net_tx_buf_mutex, -1);
        //printf("fl.f: %p\n", net_tx_buf_free_list.first);
        //printf("fl.fn: %p\n", net_tx_buf_free_list.first->next);
        tx_buf = (struct net_tx_buf_tag *)co_list_pop_front(&net_tx_buf_free_list);
        rtos_mutex_unlock(net_tx_buf_mutex);
    }
    if (!tx_buf) {
        aic_dbg("%s get buf fail\n", __func__);
        return NULL;
    }
    return (net_buf_tx_t *)&(tx_buf->pkt_hdr);
}
static err_t net_if_output(net_if_t *net_if, struct pbuf *p_buf)
{
    struct pbuf *q;
    unsigned char *ptr;
    struct eth_hdr *ethhdr;

    if (!netif_is_up(net_if)) {
        printf("net_if_output error ERR_IF \n");
        return ERR_IF;
    }

	#if 0
    static int data_tx_cnt = 0;
    static int time_cnt = 0;
	if (p_buf->tot_len > 1400) {
		data_tx_cnt++;
		if(rtos_now(0) - time_cnt > 1000) {
			AIC_LOG_PRINTF("Tx %d/s\n", data_tx_cnt);
			time_cnt = rtos_now(0);
			data_tx_cnt = 0;
		}
		pbuf_free(p_buf);
		return 0;
	}
	#endif
    // Increase the ref count so that the buffer is not freed by the networking stack
    // until it is actually sent over the WiFi interface
    pbuf_ref(p_buf);

    if (p_buf->tot_len) {
        uint32_t offset = 0;

        TCPIP_PACKET_INFO_T *sent_pkt;
        struct fhost_vif_tag *fhost_vif;
        fhost_vif = &fhost_env.vif[0];

        if (!net_tx_buf_sema)
            return -1;
    
        // uint32_t reserved_len = SDIO_HOSTDESC_SIZE;
        int ret = rtos_semaphore_wait(net_tx_buf_sema, 50);
        struct net_tx_buf_tag *tx_buf = NULL;
        if (ret == 0) {
            rtos_mutex_lock(net_tx_buf_mutex, -1);
            //printf("fl.f: %p\n", net_tx_buf_free_list.first);
            //printf("fl.fn: %p\n", net_tx_buf_free_list.first->next);
            tx_buf = (struct net_tx_buf_tag *)co_list_pop_front(&net_tx_buf_free_list);
            rtos_mutex_unlock(net_tx_buf_mutex);
        }
        if (!tx_buf) {
            aic_dbg("%s get buf fail, ret = %d\n", __func__, ret);
            return -1;
        }

        // uint8_t *buf = tx_buf->buf + reserved_len;
        // for (q = p_buf; q != NULL; q = q->next) {
        //     memcpy((void *)(buf + offset), (void *)q->payload, q->len);
        //     offset += q->len;
        // }

        //aic_dbg("%s %x\n", __func__, p_buf);
        sent_pkt = (TCPIP_PACKET_INFO_T *)&(tx_buf->pkt_hdr);
        sent_pkt->data_ptr = p_buf;
        sent_pkt->data_len = p_buf->tot_len;
        sent_pkt->net_id   = 0;
        sent_pkt->pkt_type = 0;
        //printf("es:%u, tid:%u\n", send_count, cyg_thread_get_id(cyg_thread_self()));
        fhost_tx_start(&fhost_vif->net_if, sent_pkt, NULL, NULL);
    }

    return ERR_OK;
}
#else
static err_t net_if_output(net_if_t *net_if, struct pbuf *p_buf)
{
    struct pbuf *q;
    unsigned char *ptr;
    struct eth_hdr *ethhdr;

    if (!netif_is_up(net_if)) {
        printf("net_if_output error ERR_IF \n");
        return ERR_IF;
    }

    //if (p_buf->tot_len > 1000)
    //    return 0;

    if (p_buf->tot_len) {
        uint32_t offset = 0;

        TCPIP_PACKET_INFO_T *sent_pkt;
        struct fhost_vif_tag *fhost_vif;
        fhost_vif = &fhost_env.vif[0];

        if (!net_tx_buf_sema)
            return ;
    
        uint32_t reserved_len = SDIO_HOSTDESC_SIZE;
        int ret = rtos_semaphore_wait(net_tx_buf_sema, 50);
        struct net_tx_buf_tag *tx_buf = NULL;
        if (ret == 0) {
            rtos_mutex_lock(net_tx_buf_mutex, -1);
            //printf("fl.f: %p\n", net_tx_buf_free_list.first);
            //printf("fl.fn: %p\n", net_tx_buf_free_list.first->next);
            tx_buf = (struct net_tx_buf_tag *)co_list_pop_front(&net_tx_buf_free_list);
            rtos_mutex_unlock(net_tx_buf_mutex);
        }
        if (!tx_buf) {
            aic_dbg("%s get buf fail, ret = %d\n", __func__, ret);
            return;
        }

        uint8_t *buf = tx_buf->buf + reserved_len;

        for (q = p_buf; q != NULL; q = q->next) {
            memcpy((void *)(buf + offset), (void *)q->payload, q->len);
            offset += q->len;
        }

        #ifdef CONFIG_ECOS_SEND_TCP_PARSE
        struct ether_header *ehdr = (struct ether_header *)buf;
        if (ntohs(ehdr->ether_type) == ETHERTYPE_IP) {
            struct ip *ihdr = (struct ip *)(buf + sizeof(struct ether_header));
            if (ihdr->ip_p == IPPROTO_TCP) {
                struct tcphdr *thdr = (struct tcphdr *)(buf + sizeof(struct ether_header) + ihdr->ip_hl * 4);
                //aic_dbg("tx:%u %u %u\n", ntohl(thdr->th_seq), ntohl(thdr->th_ack), ntohs(thdr->th_win));
                aic_dbg("tx:%u %u\n", ntohl(thdr->th_seq), ntohl(thdr->th_ack));
            }
        }
        #endif

        sent_pkt = (TCPIP_PACKET_INFO_T *)&(tx_buf->pkt_hdr);
        sent_pkt->data_ptr = tx_buf->buf + reserved_len;
        sent_pkt->data_len = offset;
        sent_pkt->net_id   = 0;
        sent_pkt->pkt_type = 0;
        //printf("es:%u, tid:%u\n", send_count, cyg_thread_get_id(cyg_thread_self()));
        fhost_tx_start(&fhost_vif->net_if, sent_pkt, NULL, NULL);

    }

    return ERR_OK;
}
#endif
#endif
static char netif_num = 0;
/**
 ****************************************************************************************
 * @brief Callback used by the networking stack to setup the network interface.
 * This function should be passed as a parameter to netifapi_netif_add().
 *
 * @param[in] net_if Pointer to the network interface to setup
 * @param[in] p_buf  Pointer to the buffer to transmit
 *
 * @return ERR_OK upon successful setup of the interface, other status otherwise
 ****************************************************************************************
 */
err_t net_if_init(net_if_t *net_if)
{
    err_t status = ERR_OK;
    struct fhost_vif_tag *vif;

    //net_if->vif = &fhost_env.vif[0];
#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    net_if->hostname = "AicWlan";
#endif
    net_if->name[ 0 ] = 'w';
    net_if->name[ 1 ] = 'l';

    vif = &fhost_env.vif[0];

    #if 1
    net_if->output = etharp_output;
    net_if->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_IGMP;
    net_if->hwaddr_len = ETHARP_HWADDR_LEN;
    net_if->mtu = LLC_ETHER_MTU;
    net_if->linkoutput = net_if_output;
    #endif
    memcpy(&vif->mac_addr, get_mac_address(), 6);
    memcpy(net_if->hwaddr, &vif->mac_addr, 6);
#if LWIP_IPV6
    net_if->output_ip6 = ethip6_output;
    net_if->ip6_autoconfig_enabled = 1;
    netif_create_ip6_linklocal_address(net_if, 1);
    net_if->flags |= NETIF_FLAG_MLD6;
    aic_dbg(" net_if_init %x net_if->flags %x\r\n", net_if, net_if->flags);
#endif

    return status;
}

int net_if_add(net_if_t *net_if,
               const uint32_t *ipaddr,
               const uint32_t *netmask,
               const uint32_t *gw,
               struct fhost_vif_tag *vif)
{
    err_t status;

    #if 1
    status = netifapi_netif_add(net_if,
                               (const ip4_addr_t *)ipaddr,
                               (const ip4_addr_t *)netmask,
                               (const ip4_addr_t *)gw,
                               vif,
                               net_if_init,
                               tcpip_input);
    #endif
    // net_if->num  = netif_num++;

    if (status == ERR_OK) {
        set_netif(MODE_STA, net_if);
    }

	AIC_LOG_PRINTF("net_if_add %s return %d\n", net_if->name, status);
    return (status == ERR_OK ? 0 : -1);
}
//uint16_t net_ip_chksum(const void *dataptr, int len)
//{
//    // Simply call the LwIP function
//    //return lwip_standard_chksum(dataptr, len);
//}

const uint8_t *net_if_get_mac_addr(net_if_t *net_if)
{
    return (uint8_t *)net_if->hwaddr;
}

net_if_t *net_if_find_from_name(const char *name)
{
    return netif_find(name);
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
    if ( len > 3)
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
    netifapi_netif_set_up(net_if);
}

void net_if_down(net_if_t *net_if)
{
    netifapi_netif_set_down(net_if);
}

void net_if_set_default(net_if_t *net_if)
{
    netifapi_netif_set_default(net_if);
}

void net_if_set_ip(net_if_t *net_if, uint32_t ip, uint32_t mask, uint32_t gw)
{
    if (!net_if)
        return;
    netif_set_addr(net_if, (const ip4_addr_t *)&ip, (const ip4_addr_t *)&mask,
                   (const ip4_addr_t *)&gw);
}

int net_if_get_ip(net_if_t *net_if, uint32_t *ip, uint32_t *mask, uint32_t *gw)
{
    if (!net_if)
        return -1;
#if 1
    if (ip)
        *ip = netif_ip4_addr(net_if)->addr;
    if (mask)
        *mask = netif_ip4_netmask(net_if)->addr;
    if (gw)
        *gw = netif_ip4_gw(net_if)->addr;
#endif
    return 0;
}

int net_if_input(net_buf_rx_t *buf, net_if_t *net_if, void *addr, uint16_t len, net_buf_free_fn free_fn)
{
    struct pbuf* p;
    AIC_LOG_ERROR("%s\n", __func__);
#if 0
    buf->custom_free_function = (pbuf_free_custom_fn)free_fn;
    p = pbuf_alloced_custom(PBUF_RAW, len, PBUF_REF, buf, addr, len);
    ASSERT_ERR(p != NULL);

    if (net_if->input(p, net_if))
    {
        free_fn(buf);
        return -1;
    }
#endif
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
    if(!buf) {
        AIC_LOG_ERROR("%s tx buffer null\n", __func__);
        return NULL;
    }
    memset(buf, 0, (sizeof(net_buf_tx_t)));
    uint8_t *payload_buf = (uint8_t *)buf + CO_ALIGN4_HI(sizeof(net_buf_tx_t));
    memcpy((payload_buf + reserved_len), payload, length);
    buf->data_ptr = (payload_buf + reserved_len);
    buf->data_len = length;
    buf->pkt_type = 0xFF;

    //AIC_LOG_PRINTF("%s tcpip %p _buf %p %p\n", __func__, buf, payload_buf, buf->data_ptr);
    //AIC_LOG_PRINTF("nbta:%p/%p\n", buf, buf->data_ptr);

    return buf;
}

void net_buf_tx_info(net_buf_tx_t *buf, uint16_t *tot_len, uint8_t *seg_cnt)
{
    #if 0
    uint8_t  idx;
    uint16_t length = buf->tot_len;

    *tot_len = length;

    idx = 0;
    while (length && buf)
    {
        // Sanity check - the payload shall be in shared RAM
        //ASSERT_ERR(!TST_SHRAM_PTR(buf->payload));

        length -= buf->len;
        idx++;
        // Get info of extra segments if any
        buf = buf->next;
    }

    *seg_cnt = idx;
    if (length != 0)
    {
        // The complete buffer must be included in all the segments
        ASSERT_ERR(0);
    }
    #endif
    *tot_len = buf->data_len;
    *seg_cnt = 1;
}

void net_buf_tx_free(net_buf_tx_t *buf)
{
    if (!buf) {
        return ;
    }
    //AIC_LOG_PRINTF("%s tcpip %x %x %x\n", __func__, buf, buf->data_ptr, buf->pkt_type);
    if (0xFF == buf->pkt_type) {
        //AIC_LOG_PRINTF("nbtf:%p/%p\n", buf, buf->data_ptr);
        rtos_free(buf);
        buf = NULL;
    } else {
        //buf->data_ptr -= (offsetof(struct hostdesc, cfm_cb));
        //buf->data_ptr -= sizeof(struct co_list_hdr);
        uint8_t *list_hdr = (uint8_t *)buf;
        list_hdr -= sizeof(struct co_list_hdr);
        #ifdef CONFIG_TX_NOCOPY
        struct pbuf *t_pbuf = (struct pbuf *)buf->data_ptr;
        //AIC_LOG_PRINTF("%s buf %x, t_pbuf %x\n", __func__, list_hdr, t_pbuf);
        pbuf_free(t_pbuf);
        #endif

        rtos_mutex_lock(net_tx_buf_mutex, -1);
        co_list_push_back(&net_tx_buf_free_list, (struct co_list_hdr *)(list_hdr));
        rtos_mutex_unlock(net_tx_buf_mutex);
        rtos_semaphore_signal(net_tx_buf_sema, 0);
    }
}

uint32_t net_buf_tx_cnt(void) {
    rtos_mutex_lock(net_tx_buf_mutex, -1);
    uint32_t cnt = co_list_cnt(&net_tx_buf_free_list);
    rtos_mutex_unlock(net_tx_buf_mutex);
    return cnt;
}

int net_init(void)
{
    uint16_t i;
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
    for(i = 0; i < NET_TXBUF_CNT; i++)
    {
        struct net_tx_buf_tag *net_tx_buffer = rtos_malloc(sizeof(struct net_tx_buf_tag));
        if(net_tx_buffer) {
            //AIC_LOG_PRINTF("%s net_buf %d %x\n", __func__, i, (net_tx_buffer));
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
    if(l2_mutex) {
        rtos_mutex_delete(l2_mutex);
        l2_mutex = NULL;
    }

    if (net_tx_buf_sema) {
        rtos_semaphore_delete(net_tx_buf_sema);
        net_tx_buf_sema = NULL;
    }
    if (net_tx_buf_mutex) {
        rtos_mutex_lock(net_tx_buf_mutex, -1);
        struct net_tx_buf_tag *net_tx_buffer = co_list_pop_front(&net_tx_buf_free_list);
        while (net_tx_buffer) {
            rtos_free(net_tx_buffer);
            net_tx_buffer = co_list_pop_front(&net_tx_buf_free_list);
        }
       rtos_mutex_unlock(net_tx_buf_mutex);
       //printf("fl.f: %p\n", net_tx_buf_free_list.first);
       //printf("fl.fn: %p\n", net_tx_buf_free_list.first->next);
       rtos_mutex_delete(net_tx_buf_mutex);
       net_tx_buf_mutex = NULL;
    }

    return 0;
}
extern void print_hex_dump_bytes(const void *addr, unsigned int len);

#ifndef CONFIG_RX_NOCOPY
int rx_eth_data_process(unsigned char *pdata,
                     unsigned short len, net_if_t *netif)
{
    struct pbuf* p;

    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p != NULL) {
        memcpy(p->payload, pdata, len);
        //print_hex_dump_bytes(pdata, len);
        if (netif->input(p, netif) != ERR_OK) {
          pbuf_free(p);
        }
    }
    return 0;
}
#else /* CONFIG_RX_NOCOPY */
int rx_eth_data_process(unsigned char *pdata,
                     unsigned short len, net_if_t *netif, bool is_pbuf)
{
    struct pbuf* p;
    if (!is_pbuf) {
        p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
        if (p != NULL) {
            memcpy(p->payload, pdata, len);
            // rwnx_data_dump("src", pdata, 32);
            // f133_dma_copy(p->payload, pdata, len);
            // rwnx_data_dump("dst", p->payload, 32);
            if (netif->input(p, netif) != ERR_OK) {
                pbuf_free(p);
            }
        }
    } else {
        p = (struct pbuf*)pdata;
        if (netif->input((struct pbuf*)p, netif) != ERR_OK) {
            pbuf_free(p);
        }
    }

    return 0;
}

struct pbuf* rx_eth_pbuf_alloc(unsigned short len)
{
    struct pbuf* p = NULL;

    p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

    // aic_dbg("A %x\r\n", p);
    return p;
}

void rx_eth_pbuf_free(struct pbuf* p)
{
    // aic_dbg("F %x\r\n", p);
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
    #ifdef CONFIG_TX_NOCOPY
    // net_buf_tx_t *net_buf;
    struct pbuf *p_buf = NULL;
    TCPIP_PACKET_INFO_T *sent_pkt;

    if (net_if == NULL || data == NULL /* || data_len >= net_if->mtu || !netif_is_up(net_if) */)
        return -1;

    p_buf = pbuf_alloc(PBUF_TRANSPORT, data_len, PBUF_RAM);
    if (p_buf == NULL)
        return 0;

    memcpy(p_buf->payload , data, data_len);
    if (dst_addr)
    {
        // Need to add ethernet header as fhost_tx_start is called directly
        struct mac_eth_hdr* ethhdr;
        if (pbuf_header(p_buf, sizeof(*ethhdr))){
            pbuf_free(p_buf);
            return -1;
        }
        ethhdr = (struct mac_eth_hdr*)p_buf->payload;
        ethhdr->type = htons(ethertype);
        memcpy(&ethhdr->da, dst_addr, 6);
        memcpy(&ethhdr->sa, net_if->hwaddr, 6);
    }
        //aic_dbg("%s %x\n", __func__, p_buf);
// aic_dbg("%s p_buf->tot_len = %d\n", __func__, p_buf->tot_len);
// print_hex_dump_bytes(p_buf->payload, p_buf->tot_len);
    int ret = rtos_semaphore_wait(net_tx_buf_sema, 5);
    struct net_tx_buf_tag *tx_buf = NULL;
    if (ret == 0) {
        rtos_mutex_lock(net_tx_buf_mutex, -1);
        //printf("fl.f: %p\n", net_tx_buf_free_list.first);
        //printf("fl.fn: %p\n", net_tx_buf_free_list.first->next);
        tx_buf = (struct net_tx_buf_tag *)co_list_pop_front(&net_tx_buf_free_list);
        rtos_mutex_unlock(net_tx_buf_mutex);
    }
    if (!tx_buf) {
        aic_dbg("%s get buf fail, ret = %d\n", __func__, ret);
        return;
    }

    // uint8_t *buf = tx_buf->buf + reserved_len;
    // for (q = p_buf; q != NULL; q = q->next) {
    //     memcpy((void *)(buf + offset), (void *)q->payload, q->len);
    //     offset += q->len;
    // }

    sent_pkt = (TCPIP_PACKET_INFO_T *)&(tx_buf->pkt_hdr);
    sent_pkt->data_ptr = p_buf;
    sent_pkt->data_len = p_buf->tot_len;
    sent_pkt->net_id   = 0;
    sent_pkt->pkt_type = 0;
    // Ensure no other thread will program a L2 transmission while this one is waiting
    // for its confirmation
    rtos_mutex_lock(l2_mutex, -1);

    //AIC_LOG_PRINTF("%s pkt %x %x, t:%x\n", __func__, net_buf, net_buf->data_ptr, net_buf->pkt_type);
    // In order to implement this function as blocking until the completion of the frame
    // transmission, directly call fhost_tx_start with a confirmation callback.
    res = fhost_tx_start(net_if, sent_pkt, net_l2_send_cfm, ack);

    // Wait for the transmission completion
    rtos_semaphore_wait(l2_semaphore, 500);

    // Now new L2 transmissions are possible
    rtos_mutex_unlock(l2_mutex);
    #else
        net_buf_tx_t *net_buf;

    if (net_if == NULL || data == NULL /* || data_len >= net_if->mtu || !netif_is_up(net_if) */)
        return -1;

    net_buf = net_buf_tx_alloc((data - sizeof(struct mac_eth_hdr)), (data_len + sizeof(struct mac_eth_hdr)));
    if (net_buf == NULL)
        return 0;

    if (dst_addr)
    {
        // Need to add ethernet header as fhost_tx_start is called directly
        struct mac_eth_hdr* ethhdr;
        ethhdr = (struct mac_eth_hdr*)net_buf->data_ptr;
        ethhdr->type = htons(ethertype);
        memcpy(&ethhdr->da, dst_addr, 6);
        memcpy(&ethhdr->sa, net_if->hwaddr, 6);
    }
//print_hex_dump_bytes(net_buf->data_ptr, net_buf->data_len);

    // Ensure no other thread will program a L2 transmission while this one is waiting
    // for its confirmation
    rtos_mutex_lock(l2_mutex, -1);

    //AIC_LOG_PRINTF("%s pkt %x %x, t:%x\n", __func__, net_buf, net_buf->data_ptr, net_buf->pkt_type);
    // In order to implement this function as blocking until the completion of the frame
    // transmission, directly call fhost_tx_start with a confirmation callback.
    res = fhost_tx_start(net_if, net_buf, net_l2_send_cfm, ack);

    // Wait for the transmission completion
    rtos_semaphore_wait(l2_semaphore, 500);

    // Now new L2 transmissions are possible
    rtos_mutex_unlock(l2_mutex);
    #endif
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
    struct mac_eth_hdr* ethhdr = (struct mac_eth_hdr*)pdata;
    uint16_t ethertype = ntohs(ethhdr->type);
    int i;
	uint8_t *payload;

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
    fhost_wpa_rx_eapol((uint8_t*)ethhdr->sa.array, len, payload);
    return ERR_OK;
}

static int net_dhcp_started = 0;
int net_dhcp_start(int net_id)
{
    int ret = 0;
    return ret;
}

void net_dhcp_stop(net_if_t *net_if)
{
    #if LWIP_IPV4 && LWIP_DHCP
    netifapi_dhcp_stop(net_if);
    net_dhcp_started = 0;
    #endif //LWIP_IPV4 && LWIP_DHCP
}

int net_dhcp_start_status(void)
{
    return net_dhcp_started;
}

int net_dhcp_release(net_if_t *net_if)
{
    #if LWIP_IPV4 && LWIP_DHCP
    if (netifapi_dhcp_release(net_if) ==  ERR_OK)
        return 0;
    #endif //LWIP_IPV4 && LWIP_DHCP
    return -1;
}

int net_dhcp_address_obtained(net_if_t *net_if)
{
    #if LWIP_IPV4 && LWIP_DHCP
    if (dhcp_supplied_address(net_if))
        return 0;
    #endif //LWIP_IPV4 && LWIP_DHCP
    return -1;
}

int net_set_dns(uint32_t dns_server)
{
    #if LWIP_DNS
    ip_addr_t ip;
    ip_addr_set_ip4_u32(&ip, dns_server);
    dns_setserver(0, &ip);
    return 0;
    #else
    return -1;
    #endif
}

int net_get_dns(uint32_t *dns_server)
{
    #if LWIP_DNS
    const ip_addr_t *ip;

    if (dns_server == NULL)
        return -1;

    ip = dns_getserver(0);
    *dns_server = ip_addr_get_ip4_u32(ip);
    return 0;
    #else
    return -1;
    #endif
}

char* aic_inet_ntoa(struct in_addr addr)
{
  	return inet_ntoa(addr);
}

#ifndef ETHER_ADDR_LEN
#define ETHER_ADDR_LEN 6
#endif

struct	ether_header {
	u_char	ether_dhost[ETHER_ADDR_LEN];
	u_char	ether_shost[ETHER_ADDR_LEN];
	u_short	ether_type;
} __attribute__ ((aligned(1), packed));


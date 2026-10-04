/*
 * Copyright (C) 2018-2020 AICSemi Ltd.
 *
 * All Rights Reserved
 */

#ifndef NET_AL_H_
#define NET_AL_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
#include "aic_types.h"
#include "rtos_al.h"
#include "co_list.h"

#ifdef PLATFORM_ALLWIN_RT_THREAD
#include <sys/socket.h>
#include "lwip/netif.h"
#endif

/// Maximum size of a interface name (including null character)
#define NET_AL_MAX_IFNAME 4
#ifdef CONFIG_TX_NOCOPY
#define MAX_ETH_DRV_SG 60
#else
#define MAX_ETH_DRV_SG 32
#endif
#define MAX_ETH_MSG 1540

struct eth_drv_sg {
    unsigned int	buf;
    unsigned int 	len;
};

/// Net interface
typedef struct netif     net_if_t;

// data types
typedef  uint32_t     TCPIP_NETID_T;              /* TCPIP net interface ID */

// TCPIP net interface packet type
typedef enum
{
    TCPIP_PKTTYPE_NULL = 0,
    TCPIP_PKTTYPE_IP,       /* packet is encapsulated as IP */
    TCPIP_PKTTYPE_ETHER,    /* packet is encapsulated as Ethernet */
    TCPIP_PKTTYPE_PPP,      /* packet is encapsulated as PPP */
    TCPIP_PKTTYPE_IP6,      /* packet is encapsulated as IPv6 only */ 
    TCPIP_PKTTYPE_VOIP,     /* bug 323266 for IMS service */
    TCPIP_PKTTYPE_FILTER,   /* for XCAP SS, share the same PDN with AP */
    TCPIP_PKTTYPE_SIPCVOLTEAUDIO,
    TCPIP_PKTTYPE_IMSBR,    /* packet is from ims bridge */
    TCPIP_PKTTYPE_MAX
} TCPIP_PACKET_TYPE_E;

typedef struct packet_info_tag
{
    uint8_t*          data_ptr;       /* data pointer */
    uint32_t          data_len;       /* data length - full packet encapsulation length */
    TCPIP_PACKET_TYPE_E pkt_type;   /* packet type */
    TCPIP_NETID_T   net_id;         /* net interface ID */
    uint32_t          link_handle;    /* link layer handle */
    struct packet_info_tag * frag_next; /* pointer to the next frag, if it has the frag */
} TCPIP_PACKET_INFO_T;

/// Net buffer
typedef int  net_buf_rx_t;

/// Net TX buffer
typedef TCPIP_PACKET_INFO_T       net_buf_tx_t;

/// Prototype for a function to free a network buffer */
typedef void (*net_buf_free_fn)(void *net_buf);

// Forward declarations
struct fhost_vif_tag;

#define  close(s)                lwip_close(s)

struct ecos_send_desc_tag {
    struct co_list_hdr hdr;
    unsigned long key;
};

#if 0
#ifndef CONFIG_LWIP
/** 255.255.255.255 */
#define IPADDR_NONE         ((unsigned int)0xffffffffUL)
/** 127.0.0.1 */
#define IPADDR_LOOPBACK     ((unsigned int)0x7f000001UL)
/** 0.0.0.0 */
#define IPADDR_ANY          ((unsigned int)0x00000000UL)
/** 255.255.255.255 */
#define IPADDR_BROADCAST    ((unsigned int)0xffffffffUL)
#endif

#ifndef CONFIG_LWIP
struct netif {
    /** descriptive abbreviation */
    char name[2];
      /** number of this interface */
    char num;
    /* the hostname for this netif, NULL is a valid value */
    const char*  hostname;
    /** link level hardware address of this interface */
    char hwaddr[6];

    void *vif;
    int net_id;
};
#endif

/// Net interface
typedef struct netif     net_if_t;
#endif

/*
 * FUNCTIONS
 ****************************************************************************************
 */
/**
 ****************************************************************************************
 * @brief Call the checksum computation function of the TCP/IP stack
 *
 * @param[in] dataptr Pointer to the data buffer on which the checksum is computed
 * @param[in] len Length of the data buffer
 *
 * @return The computed checksum
 ****************************************************************************************
 */
uint16_t net_ip_chksum(const void *dataptr, int len);

/**
 ****************************************************************************************
 * @brief Add a network interface
 *
 * @param[in] net_if Pointer to the net_if structure to add
 * @param[in] ipaddr Pointer to the IP address of the interface (NULL if not available)
 * @param[in] netmask Pointer to the net mask of the interface (NULL if not available)
 * @param[in] gw Pointer to the gateway address of the interface (NULL if not available)
 * @param[in] vif Pointer to the VIF information structure associated to this interface
 *
 * @return 0 on success and != 0 if error occurred
 ****************************************************************************************
 */
int net_if_add(net_if_t *net_if,
               const uint32_t *ipaddr,
               const uint32_t *netmask,
               const uint32_t *gw,
               struct fhost_vif_tag *vif);

/**
 ****************************************************************************************
 * @brief Get network interface MAC address
 *
 * @param[in] net_if Pointer to the net_if structure
 *
 * @return Pointer to interface MAC address
 ****************************************************************************************
 */
const uint8_t *net_if_get_mac_addr(net_if_t *net_if);

/**
 ****************************************************************************************
 * @brief Get pointer to network interface from its name
 *
 * @param[in] name Name of the interface
 *
 * @return pointer to the net_if structure and NULL if such interface doesn't exist.
 ****************************************************************************************
 */
net_if_t *net_if_find_from_name(const char *name);

/**
 ****************************************************************************************
 * @brief Get pointer to network interface from its FHOST wifi index
 *
 * @param[in] idx Index of the FHOST wifi interface
 *
 * @return pointer to the net_if structure and NULL if such interface doesn't exist.
 ****************************************************************************************
 */
net_if_t *net_if_find_from_wifi_idx(unsigned int idx);

/**
 ****************************************************************************************
 * @brief Get name of network interface
 *
 * Passing a buffer of at least @ref NET_AL_MAX_IFNAME bytes, will ensure that it is big
 * enough to contain the interface name.
 *
 * @param[in]     net_if Pointer to the net_if structure
 * @param[in]     buf    Buffer to write the interface name
 * @param[in,out] len    Length of the buffer, updated with the actual length of the
 *                       interface name (not including terminating null byte)
 *
 * @return 0 on success and != 0 if error occurred
 ****************************************************************************************
 */
int net_if_get_name(net_if_t *net_if, char *buf, int len);

/**
 ****************************************************************************************
 * @brief Get index of the FHOST wifi interface for a network interface
 *
 * @param[in] net_if Pointer to the net_if structure
 *
 * @return <0 if cannot find the interface and the FHOST wifi interface index otherwise
 ****************************************************************************************
 */
int net_if_get_wifi_idx(net_if_t *net_if);

/**
 ****************************************************************************************
 * @brief Indicate that the network interface is now up (i.e. able to do traffic)
 *
 * @param[in] net_if Pointer to the net_if structure
 ****************************************************************************************
 */
void net_if_up(net_if_t *net_if);

/**
 ****************************************************************************************
 * @brief Indicate that the network interface is now down
 *
 * @param[in] net_if Pointer to the net_if structure
 ****************************************************************************************
 */
void net_if_down(net_if_t *net_if);

/**
 ****************************************************************************************
 * @brief Set a network interface as the default output interface
 *
 * If IP routing failed to select an output interface solely based on interface and
 * destination addresses then this interface will be selected.
 * Use a NULL parameter to reset the default interface.
 *
 * @param[in] net_if Pointer to the net_if structure
 ****************************************************************************************
 */
void net_if_set_default(net_if_t *net_if);

/**
 ****************************************************************************************
 * @brief Set IPv4 address of an interface
 *
 * It is assumed that only one address can be configured on the interface and then
 * setting a new address can be used to replace/delete the current address.
 *
 * @param[in] net_if Pointer to the net_if structure
 * @param[in] ip     IPv4 address
 * @param[in] mask   IPv4 network mask
 * @param[in] gw     IPv4 gateway address
 ****************************************************************************************
 */
void net_if_set_ip(net_if_t *net_if, uint32_t ip, uint32_t mask, uint32_t gw);

/**
 ****************************************************************************************
 * @brief Get IPv4 address of an interface
 *
 * Set to NULL parameter you're not interested in.
 *
 * @param[in]  net_if Pointer to the net_if structure
 * @param[out] ip     IPv4 address
 * @param[out] mask   IPv4 network mask
 * @param[out] gw     IPv4 gateway address
 * @return 0 if requested parameters have been updated successfully and !=0 otherwise.
 ****************************************************************************************
 */
int net_if_get_ip(net_if_t *net_if, uint32_t *ip, uint32_t *mask, uint32_t *gw);

/**
 ****************************************************************************************
 * @brief Call the networking stack input function.
 * This function is supposed to link the payload data and length to the RX buffer
 * structure passed as parameter. The free_fn function shall be called when the networking
 * stack is not using the buffer anymore.
 *
 * @param[in] buf Pointer to the RX buffer structure
 * @param[in] net_if Pointer to the net_if structure that receives the packet
 * @param[in] addr Pointer to the payload data
 * @param[in] len Length of the data available at payload address
 * @param[in] free_fn Pointer to buffer freeing function to be called after use
 *
 * @return 0 on success and != 0 if packet is not accepted
 ****************************************************************************************
 */
int net_if_input(net_buf_rx_t *buf, net_if_t *net_if, void *addr, uint16_t len, net_buf_free_fn free_fn);

/**
 ****************************************************************************************
 * @brief Get the pointer to the VIF structure attached to a net interface.
 *
 * @param[in] net_if Pointer to the net_if structure
 *
 * @return The pointer to the VIF structure attached to a net interface
 ****************************************************************************************
 */
struct fhost_vif_tag *net_if_vif_info(net_if_t *net_if);

 /**
  ****************************************************************************************
  * @brief Allocate a buffer for TX.
  *
  * This function is used to transmit buffer that do not originate from the Network Stack.
  * (e.g. a management frame sent by wpa_supplicant)
  * This function allocates a buffer for transmission and initializes its content with
  * the payload passed as parameter. The buffer must still reserve @ref NET_AL_TX_HEADROOM
  * headroom space like for regular TX buffers.
  *
  * @param[in] payload  Pointer to the buffer data
  * @param[in] length   Size, in bytes, of the payload
  *
  * @return The pointer to the allocated TX buffer and NULL is allocation failed
  ****************************************************************************************
  */
net_buf_tx_t *net_buf_tx_alloc(const uint8_t *payload, uint32_t length);

/**
 ****************************************************************************************
 * @brief Provides information on a TX buffer.
 *
 * This function is used by the FHOST module before queuing the buffer for transmission.
 * This function must returns information (pointer and length) on all data segments that
 * compose the buffer. Each buffer must at least have one data segment.
 * It must also return a pointer to the headroom (of size @ref NET_AL_TX_HEADROOM)
 * which must be reserved for each TX buffer on their first data segment.
 *
 * @param[in]     buf       Pointer to the TX buffer structure
 * @param[in]     tot_len   Total size in bytes on the buffer (includes size of all data
 *                          segment)
 * @param[in,out] seg_cnt   Contains the maximum number of data segment supported (i.e.
 *                          the size of @p seg_addr and @p seg_len parameter) and must be
 *                          updated with the actual number of segment in this buffer.
 * @param[out]    seg_addr  Table to retrieve the address of each segment.
 * @param[out]    seg_len   Table to retrieve the length, in bytes, of each segment.
 ****************************************************************************************
 */
void net_buf_tx_info(net_buf_tx_t *buf, uint16_t *tot_len, uint8_t *seg_cnt);

/**
 ****************************************************************************************
 * @brief Free a TX buffer that was involved in a transmission.
 *
 * @param[in] buf Pointer to the TX buffer structure
 ****************************************************************************************
 */
void net_buf_tx_free(net_buf_tx_t *buf);

/**
 ****************************************************************************************
 * @brief Initialize the networking stack
 *
 * @return 0 on success and != 0 if packet is not accepted
 ****************************************************************************************
 */
int net_init(void);
int net_deinit(void);

/**
 ****************************************************************************************
 * @brief Send a L2 (aka ethernet) packet
 *
 * Send data on the link layer (L2). If destination address is not NULL, Ethernet header
 * will be added (using ethertype parameter) and MAC address of the sending interface is
 * used as source address. If destination address is NULL, it means that ethernet header
 * is already present and frame should be send as is.
 * The data buffer will be copied by this function, and must then be freed by the caller.
 *
 * The primary purpose of this function is to allow the supplicant sending EAPOL frames.
 * As these frames are often followed by addition/deletion of crypto keys, that
 * can cause encryption to be enabled/disabled in the MAC, it is required to ensure that
 * the packet transmission is completed before proceeding to the key setting.
 * This function shall therefore be blocking until the frame has been transmitted by the
 * MAC.
 *
 * @param[in] net_if    Pointer to the net_if structure.
 * @param[in] data      Data buffer to send.
 * @param[in] data_len  Buffer size, in bytes.
 * @param[in] ethertype Ethernet type to set in the ethernet header. (in host endianess)
 * @param[in] dst_addr  Ethernet address of the destination. If NULL then it means that
 *                      ethernet header is already present in the frame (and in this case
 *                      ethertype should be ignored)
 * @param[out] ack      Optional to get transmission status. If not NULL, the value
 *                      pointed is set to true if peer acknowledged the transmission and
 *                      false in all other cases.
 *
 * @return 0 on success and != 0 if packet hasn't been sent
 ****************************************************************************************
 */
int net_l2_send(net_if_t *net_if, const uint8_t *data, int data_len, uint16_t ethertype,
                const uint8_t *dst_addr, bool *ack);

/**
 ****************************************************************************************
 * @brief Create a L2 (aka ethernet) socket for specific packet
 *
 * Create a L2 socket that will receive specified frames: a given ethertype on a given
 * interface.
 * It is expected to fail if a L2 socket for the same ethertype/interface couple already
 * exists.
 *
 * @note As L2 sockets are not specified in POSIX standard, the implementation of such
 * function may be impossible in some network stack.
 *
 * @param[in] net_if    Pointer to the net_if structure.
 * @param[in] ethertype Ethernet type to filter. (in host endianess)
 *
 * @return <0 if error occurred and the socket descriptor otherwise.
 ****************************************************************************************
 */
int net_l2_socket_create(net_if_t *net_if, uint16_t ethertype);

/**
 ****************************************************************************************
 * @brief Delete a L2 (aka ethernet) socket
 *
 * @param[in] sock Socket descriptor returned by @ref net_l2_socket_create
 *
 * @return 0 on success and != 0 if error occurred.
 ****************************************************************************************
 **/
int net_l2_socket_delete(int sock);

/**
 ****************************************************************************************
 * @brief Start DHCP procedure on a given interface
 *
 * @param[in] net_if Pointer to the interface on which DHCP must be started
 *
 * @return 0 if DHCP procedure successfully started and != 0 if an error occured
 ****************************************************************************************
 */
int net_dhcp_start(int net_id);

/**
 ****************************************************************************************
 * @brief Stop DHCP procedure on a given interface
 *
 * @param[in] net_if Pointer to the interface on which DHCP must be stoped
 ****************************************************************************************
 */
void net_dhcp_stop(net_if_t *net_if);

/**
 ****************************************************************************************
 * @brief Release DHCP lease on a given interface
 *
 * @note Not needed if NET_AL_NO_IP is defined
 *
 * @param[in] net_if Pointer to the interface on which DHCP must be released
 * @return 0 if DHCP lease has been released and != 0 if an error occurred
 ****************************************************************************************
 */
int net_dhcp_release(net_if_t *net_if);

/**
 ****************************************************************************************
 * @brief Check if an IP has been assigned with DHCP
 *
 * @param[in] net_if Pointer to the interface to test
 *
 * @return 0 if ip address assigned to the interface has been obtained via DHCP and != 0
 * otherwise
 ****************************************************************************************
 */
int net_dhcp_address_obtained(net_if_t *net_if);

/**
 ****************************************************************************************
 * @brief Configure DNS server IP address
 *
 * @note Not needed if NET_AL_NO_IP is defined
 *
 * @param[in] dns_server  DNS server IPv4 address
 * @return 0 on success and != 0 if error occurred.
 ****************************************************************************************
 */
int net_set_dns(uint32_t dns_server);

/**
 ****************************************************************************************
 * @brief Get DNS server IP address
 *
 * @note Not needed if NET_AL_NO_IP is defined
 *
 * @param[out] dns_server  DNS server IPv4 address
 * @return 0 on success and != 0 if error occurred.
 ****************************************************************************************
 */
int net_get_dns(uint32_t *dns_server);
err_t net_if_init(struct netif *net_if);
err_t net_eth_receive(unsigned char *pdata, unsigned short len, struct netif *netif);
#ifdef CONFIG_RX_NOCOPY
int rx_eth_data_process(unsigned char *pdata, unsigned short len, net_if_t *netif, bool is_pbuf);
#else
int rx_eth_data_process(unsigned char *pdata, unsigned short len, struct netif *netif);
#endif
int net_dhcp_start_status(void);
char* aic_inet_ntoa(struct in_addr addr);

struct pbuf* rx_eth_pbuf_alloc(unsigned short len);
void rx_eth_pbuf_free(struct pbuf* p);
#endif // NET_AL_H_

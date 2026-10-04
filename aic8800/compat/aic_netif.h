/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * The network interface of a virtual interface of the WiFi core. The IP stack
 * side of it is a Zephyr net_if, kept behind the zif pointer.
 */

#ifndef AIC_NETIF_H_
#define AIC_NETIF_H_

#include <stdint.h>

/* integer types the sources use by their lwIP names */
#ifndef AIC_LWIP_TYPES
#define AIC_LWIP_TYPES
typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
#endif

#define NETIF_FLAG_UP		0x01U
#define NETIF_FLAG_BROADCAST	0x02U
#define NETIF_FLAG_LINK_UP	0x04U
#define NETIF_FLAG_ETHARP	0x08U
#define NETIF_FLAG_IGMP		0x80U

#define ETHARP_HWADDR_LEN	6

#ifndef ERR_OK
#define ERR_OK	 0
#define ERR_MEM	 (-1)
#define ERR_BUF	 (-2)
#define ERR_IF	 (-12)
#endif

struct netif {
	struct netif *next;
	char name[2];
	char num;
	uint8_t hwaddr_len;
	uint8_t hwaddr[6];
	uint16_t mtu;
	uint8_t flags;
	/** The virtual interface (struct fhost_vif_tag) this interface belongs to */
	void *state;
	/** Zephyr network interface */
	void *zif;
};

#endif /* AIC_NETIF_H_ */

/* SPDX-License-Identifier: Apache-2.0 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/net_if.h>
#include <zephyr/random/random.h>

#include <aic8800/aic8800.h>
#include <aic8800/p2p.h>
#include "aic_zif.h"

static K_MUTEX_DEFINE(p2p_lock);
static bool active;

int aic8800_p2p_start(const char *name, uint16_t rtsp_port)
{
	if (!name || !*name || strlen(name) > 32 || !rtsp_port) {
		return -EINVAL;
	}
	for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
		if (*p < 32 || *p >= 127) {
			return -EINVAL;
		}
	}
	k_mutex_lock(&p2p_lock, K_FOREVER);
	if (!aic8800_running()) {
		active = false;
	}
	if (active || aic8800_running()) {
		k_mutex_unlock(&p2p_lock);
		return -EBUSY;
	}
	static const char hex[] = "0123456789abcdef";
	char passphrase[33];
	uint8_t random[16];
	struct net_if *iface = aicz_zif();
	struct in_addr address, mask, first;
	int ret;

	if (!iface) {
		k_mutex_unlock(&p2p_lock);
		return -ENODEV;
	}
	sys_rand_get(random, sizeof(random));
	for (size_t i = 0; i < sizeof(random); i++) {
		passphrase[2 * i] = hex[random[i] >> 4];
		passphrase[2 * i + 1] = hex[random[i] & 15];
	}
	passphrase[32] = 0;
	ret = aicz_p2p_start(name, passphrase, rtsp_port);
	memset(passphrase, 0, sizeof(passphrase));
	if (ret) {
		k_mutex_unlock(&p2p_lock);
		return ret;
	}
	net_addr_pton(AF_INET, "192.168.49.1", &address);
	net_addr_pton(AF_INET, "255.255.255.0", &mask);
	net_addr_pton(AF_INET, "192.168.49.2", &first);
	aicz_zif_dhcp_stop(iface);
	ret = aicz_zif_ip_set(iface, address.s_addr, mask.s_addr, 0);
	if (ret) {
		goto fail;
	}
	net_if_up(iface);
	aicz_zif_carrier(iface, true);
	ret = net_dhcpv4_server_start(iface, &first);
	if (ret) {
		goto fail;
	}
	active = true;
	k_mutex_unlock(&p2p_lock);
	return 0;
fail:
	if (aic8800_running()) {
		aic8800_stop();
	}
	k_mutex_unlock(&p2p_lock);
	return ret;
}

int aic8800_p2p_accept(void)
{
	return aicz_p2p_accept();
}

static void lease(struct net_if *iface, struct dhcpv4_addr_slot *entry, void *arg)
{
	struct in_addr *addr = arg;

	ARG_UNUSED(iface);
	if (!addr->s_addr && entry->state == DHCPV4_SERVER_ADDR_ALLOCATED) {
		*addr = entry->addr;
	}
}

int aic8800_p2p_peer(char *ipv4, size_t size)
{
	struct in_addr addr = {0};
	int ret;

	if (!ipv4 || size < NET_IPV4_ADDR_LEN) {
		return -EINVAL;
	}
	k_mutex_lock(&p2p_lock, K_FOREVER);
	ret = !active || !aic8800_running() ? -ENETDOWN :
		net_dhcpv4_server_foreach_lease(aicz_zif(), lease, &addr);
	if (!ret) {
		ret = !addr.s_addr ? -EAGAIN :
			net_addr_ntop(AF_INET, &addr, ipv4, size) ? 0 : -ENOSPC;
	}
	k_mutex_unlock(&p2p_lock);
	return ret;
}

int aic8800_p2p_stop(void)
{
	int ret;

	k_mutex_lock(&p2p_lock, K_FOREVER);
	if (!active) {
		k_mutex_unlock(&p2p_lock);
		return -EALREADY;
	}
	net_dhcpv4_server_stop(aicz_zif());
	active = false;
	ret = aic8800_stop();
	k_mutex_unlock(&p2p_lock);
	return ret;
}

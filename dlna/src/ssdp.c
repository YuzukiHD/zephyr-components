/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * SSDP: answers M-SEARCH requests and announces the device on 239.255.255.250:1900. The thread
 * follows the network: when the address goes away the socket is closed, and when one (a new one)
 * appears the group is joined again and the device announced.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>

#include "dlna_priv.h"
#include "upnp.h"
#include "util.h"

#define SSDP_ADDR "239.255.255.250"
#define SSDP_PORT 1900
#define TARGETS (3 + SVC_COUNT)

K_THREAD_STACK_DEFINE(ssdp_stack, CONFIG_DLNA_STACK_SIZE);
static struct k_thread ssdp_thread;
static volatile bool stop_req;
static bool thread_up;

/* the targets: root device, the uuid, the device type and each service */
static void target(int i, char *nt, size_t nts, char *usn, size_t usns)
{
	const char *uuid = upnp.cfg->uuid;

	switch (i) {
	case 0:
		snprintf(nt, nts, "upnp:rootdevice");
		snprintf(usn, usns, "uuid:%s::upnp:rootdevice", uuid);
		break;
	case 1:
		snprintf(nt, nts, "uuid:%s", uuid);
		snprintf(usn, usns, "uuid:%s", uuid);
		break;
	case 2:
		snprintf(nt, nts, UPNP_DEVICE_TYPE);
		snprintf(usn, usns, "uuid:%s::" UPNP_DEVICE_TYPE, uuid);
		break;
	default:
		upnp_service_type(i - 3, nt, nts);
		snprintf(usn, usns, "uuid:%s::%s", uuid, nt);
	}
}

static void send_to(int fd, const struct sockaddr_in *to, const char *msg)
{
	zsock_sendto(fd, msg, strlen(msg), 0, (const struct sockaddr *)to, sizeof(*to));
}

static void multicast_addr(struct sockaddr_in *a)
{
	memset(a, 0, sizeof(*a));
	a->sin_family = AF_INET;
	a->sin_port = htons(SSDP_PORT);
	zsock_inet_pton(AF_INET, SSDP_ADDR, &a->sin_addr);
}

static void notify(int fd, const char *ip, bool alive)
{
	struct sockaddr_in mc;
	char nt[96], usn[160], msg[640];

	multicast_addr(&mc);
	for (int i = 0; i < TARGETS; i++) {
		target(i, nt, sizeof(nt), usn, sizeof(usn));
		if (alive) {
			snprintf(msg, sizeof(msg),
				 "NOTIFY * HTTP/1.1\r\nHOST: " SSDP_ADDR ":1900\r\n"
				 "CACHE-CONTROL: max-age=%d\r\nLOCATION: http://%s:%d/description.xml\r\n"
				 "NT: %s\r\nNTS: ssdp:alive\r\nSERVER: " DLNA_SERVER "\r\nUSN: %s\r\n\r\n",
				 2 * CONFIG_DLNA_ANNOUNCE_INTERVAL_S, ip, CONFIG_DLNA_HTTP_PORT, nt, usn);
		} else {
			snprintf(msg, sizeof(msg),
				 "NOTIFY * HTTP/1.1\r\nHOST: " SSDP_ADDR ":1900\r\nNT: %s\r\n"
				 "NTS: ssdp:byebye\r\nUSN: %s\r\n\r\n", nt, usn);
		}
		send_to(fd, &mc, msg);
	}
}

static void answer_search(int fd, const struct sockaddr_in *from, const char *ip, const char *st)
{
	char nt[96], usn[160], msg[640];
	bool all = !strcmp(st, "ssdp:all");

	for (int i = 0; i < TARGETS; i++) {
		target(i, nt, sizeof(nt), usn, sizeof(usn));
		if (!all && strcmp(st, nt)) {
			continue;
		}
		snprintf(msg, sizeof(msg),
			 "HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=%d\r\nEXT:\r\n"
			 "LOCATION: http://%s:%d/description.xml\r\nSERVER: " DLNA_SERVER "\r\n"
			 "ST: %s\r\nUSN: %s\r\n\r\n",
			 2 * CONFIG_DLNA_ANNOUNCE_INTERVAL_S, ip, CONFIG_DLNA_HTTP_PORT, nt, usn);
		send_to(fd, from, msg);
	}
}

static void handle_packet(int fd, char *buf, const struct sockaddr_in *from, const char *ip)
{
	const char *st, *mx;
	char want[128];
	int n = 0, delay_ms;

	if (strncmp(buf, "M-SEARCH", 8)) {
		return;
	}
	st = header_find(buf, "ST");
	if (!st) {
		return;
	}
	while (st[n] && st[n] != '\r' && st[n] != '\n' && n < (int)sizeof(want) - 1) {
		n++;
	}
	memcpy(want, st, n);
	want[n] = '\0';

	/* spread the answers over part of the MX window so many devices do not answer at once */
	mx = header_find(buf, "MX");
	delay_ms = mx ? atoi(mx) : 1;
	delay_ms = (delay_ms < 1 ? 1 : (delay_ms > 3 ? 3 : delay_ms)) * 1000;
	k_msleep(sys_rand32_get() % (delay_ms / 2));
	answer_search(fd, from, ip, want);
}

static int open_socket(void)
{
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(SSDP_PORT)};
	struct ip_mreqn mr;
	struct net_if *iface = net_if_get_default();
	int fd, on = 1;

	fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		return -errno;
	}
	zsock_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if (zsock_bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) {
		int e = errno;

		zsock_close(fd);
		return -e;
	}
	memset(&mr, 0, sizeof(mr));
	zsock_inet_pton(AF_INET, SSDP_ADDR, &mr.imr_multiaddr);
	mr.imr_ifindex = net_if_get_by_iface(iface);
	/* Membership is held by the interface and can survive a socket or Wi-Fi restart. */
	if (zsock_setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0 &&
	    errno != EALREADY) {
		int e = errno;

		zsock_close(fd);
		return -e;
	}
	return fd;
}

static void ssdp_main(void *a, void *b, void *c)
{
	char ip[16] = "", now[16];
	int fd = -1;
	int64_t next_alive = 0;
	char *buf = malloc(1024);

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (!stop_req && buf) {
		bool have = dlna_local_ip(now);

		if (!have || strcmp(now, ip)) {
			/* the address went or changed: start over */
			if (fd >= 0) {
				zsock_close(fd);
				fd = -1;
			}
			ip[0] = '\0';
			if (!have) {
				k_msleep(1000);
				continue;
			}
		}
		if (fd < 0) {
			fd = open_socket();
			if (fd < 0) {
				printk("dlna: ssdp socket failed (%d)\n", fd);
				fd = -1;
				k_msleep(2000);
				continue;
			}
			strcpy(ip, now);
			next_alive = 0;
		}
		if (k_uptime_get() >= next_alive) {
			notify(fd, ip, true);
			next_alive = k_uptime_get() + CONFIG_DLNA_ANNOUNCE_INTERVAL_S * 1000LL;
		}

		struct zsock_pollfd p = {.fd = fd, .events = ZSOCK_POLLIN};

		if (zsock_poll(&p, 1, 1000) > 0 && (p.revents & ZSOCK_POLLIN)) {
			struct sockaddr_in from;
			socklen_t fl = sizeof(from);
			int n = zsock_recvfrom(fd, buf, 1023, 0, (struct sockaddr *)&from, &fl);

			if (n > 0) {
				buf[n] = '\0';
				handle_packet(fd, buf, &from, ip);
			}
		}
	}
	if (fd >= 0) {
		if (ip[0]) {
			notify(fd, ip, false);
		}
		zsock_close(fd);
	}
	free(buf);
}

int ssdp_start(void)
{
	stop_req = false;
	k_thread_create(&ssdp_thread, ssdp_stack, K_THREAD_STACK_SIZEOF(ssdp_stack), ssdp_main,
			NULL, NULL, NULL, CONFIG_DLNA_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&ssdp_thread, "dlna_ssdp");
	thread_up = true;
	return 0;
}

void ssdp_stop(void)
{
	if (!thread_up) {
		return;
	}
	stop_req = true;
	k_thread_join(&ssdp_thread, K_SECONDS(5));
	thread_up = false;
}

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Joins the network of /SD:/wifi.cfg and tries TCP connections to the servers of a video site and to
 * a few others, to see where they fail: which addresses the names give, what connect() answers
 * (refused, reset, time out) and what the server says to a request on a port that is open.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>

static FATFS fat_fs;
static struct fs_mount_t mp = {.type = FS_FATFS, .fs_data = &fat_fs, .mnt_point = "/SD:"};

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(connected, 0, 1);
static K_SEM_DEFINE(got_ip, 0, 1);

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *s = cb->info;

		printk("wifi: connect %s (%d)\n", s->status == 0 ? "ok" : "failed", s->status);
		if (s->status == 0) {
			k_sem_give(&connected);
		}
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&got_ip);
	}
}

/*
 * Every frame that arrives with a TCP segment of a server (not of the local network) with RST or
 * SYN+ACK set: who sent it (MAC), the TTL and the sequence numbers, to tell a reset of the far end
 * from one of the first hop or of the board itself.
 */
static K_THREAD_STACK_DEFINE(cap_stack, 3072);
static struct k_thread cap_thread;
static int cap_fd = -1;
static volatile bool cap_on;

static void cap_main(void *a, void *b, void *c)
{
	uint8_t f[128];

	for (;;) {
		int n = zsock_recv(cap_fd, f, sizeof(f), 0);

		if (n < 14 + 20 + 20 || f[12] != 0x08 || f[13] != 0x00) {
			continue;
		}
		{
			const uint8_t *ip = f + 14;
			int ihl = (ip[0] & 15) * 4;
			const uint8_t *t = ip + ihl;
			uint8_t flags;
			char s[NET_IPV4_ADDR_LEN], d[NET_IPV4_ADDR_LEN];

			if (ip[9] != 6 || n < 14 + ihl + 20) {
				continue;
			}
			flags = t[13];
			if (!cap_on || (!(flags & 0x04) && (flags & 0x12) != 0x12)) {
				continue;
			}
			if (ip[12] == 192 && ip[13] == 168) {
				continue;
			}
			zsock_inet_ntop(AF_INET, ip + 12, s, sizeof(s));
			zsock_inet_ntop(AF_INET, ip + 16, d, sizeof(d));
			printk("    rx %02x:%02x:%02x:%02x:%02x:%02x  %s:%u > %s:%u  ttl %u id %u  flags %s%s%s  seq %u ack %u  win %u  "
			       "tcp hdr %u\n",
			       f[6], f[7], f[8], f[9], f[10], f[11], s, (t[0] << 8) | t[1], d, (t[2] << 8) | t[3], ip[8],
			       (ip[4] << 8) | ip[5], flags & 0x02 ? "SYN " : "", flags & 0x10 ? "ACK " : "",
			       flags & 0x04 ? "RST" : "", (unsigned)((t[4] << 24) | (t[5] << 16) | (t[6] << 8) | t[7]),
			       (unsigned)((t[8] << 24) | (t[9] << 16) | (t[10] << 8) | t[11]), (t[14] << 8) | t[15],
			       (t[12] >> 4) * 4);
		}
	}
}

static void cap_start(struct net_if *iface)
{
	struct sockaddr_ll ll = {.sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_ALL),
				 .sll_ifindex = net_if_get_by_iface(iface)};

	cap_fd = zsock_socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (cap_fd < 0 || zsock_bind(cap_fd, (struct sockaddr *)&ll, sizeof(ll)) < 0) {
		printk("capture: not available (errno %d)\n", errno);
		return;
	}
	k_thread_create(&cap_thread, cap_stack, K_THREAD_STACK_SIZEOF(cap_stack), cap_main, NULL, NULL, NULL, 7, 0,
			K_NO_WAIT);
	k_thread_name_set(&cap_thread, "capture");
	cap_on = true;
}

static bool card_reinit(int tries)
{
	uint8_t sec[512] __aligned(64);

	disk_access_ioctl("SD", DISK_IOCTL_CTRL_DEINIT, NULL);
	if (disk_access_init("SD") != 0 || disk_access_read("SD", sec, 0, 1) != 0) {
		return false;
	}

	return sec[510] == 0x55 && sec[511] == 0xaa;
}

static void card_wait(void)
{
	for (int tries = 1;; tries++) {
		int ret = fs_mount(&mp);

		if (ret == 0 || ret == -EBUSY) {
			return;
		}
		printk("card: not there (%d), try %d\n", ret, tries);
		for (int i = 0; i < 5 && !card_reinit(tries * 10 + i); i++) {
			k_msleep(300);
		}
		k_msleep(500);
	}
}

static void wifi_cfg_read(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
	struct fs_file_t f;
	char buf[128];
	ssize_t n;
	char *nl;

	fs_file_t_init(&f);
	if (fs_open(&f, "/SD:/wifi.cfg", FS_O_READ) != 0) {
		return;
	}
	n = fs_read(&f, buf, sizeof(buf) - 1);
	fs_close(&f);
	if (n <= 0) {
		return;
	}
	buf[n] = '\0';
	nl = strchr(buf, '\n');
	if (nl != NULL) {
		*nl++ = '\0';
	}
	buf[strcspn(buf, "\r")] = '\0';
	snprintf(ssid, ssid_size, "%s", buf);
	if (nl != NULL) {
		nl[strcspn(nl, "\r\n")] = '\0';
		snprintf(psk, psk_size, "%s", nl);
	}
}

static bool join(void)
{
	struct net_if *iface = net_if_get_first_wifi();
	char ssid[33] = CONFIG_PROBE_WIFI_SSID, psk[65] = CONFIG_PROBE_WIFI_PSK;
	struct wifi_connect_req_params p;

	if (iface == NULL) {
		printk("no WiFi interface\n");
		return false;
	}
	if (ssid[0] == '\0') {
		card_wait();
		wifi_cfg_read(ssid, sizeof(ssid), psk, sizeof(psk));
	}
	if (ssid[0] == '\0') {
		printk("no network: put /SD:/wifi.cfg on the card\n");
		return false;
	}
	net_mgmt_init_event_callback(&wifi_cb, wifi_event, NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);
	p = (struct wifi_connect_req_params){
		.ssid = ssid, .ssid_length = strlen(ssid), .psk = psk, .psk_length = strlen(psk),
		.security = strlen(psk) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY, .band = WIFI_FREQ_BAND_UNKNOWN,
	};
	printk("wifi: joining %s\n", ssid);
	if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p)) != 0 ||
	    k_sem_take(&connected, K_SECONDS(40)) != 0 || k_sem_take(&got_ip, K_SECONDS(40)) != 0) {
		printk("wifi: no connection\n");
		return false;
	}
	{
		char a[NET_IPV4_ADDR_LEN], m[NET_IPV4_ADDR_LEN], g[NET_IPV4_ADDR_LEN];
		struct net_if_ipv4 *v4 = iface->config.ip.ipv4;
		struct dns_resolve_context *dns = dns_resolve_get_default();

		net_addr_ntop(AF_INET, &v4->unicast[0].ipv4.address.in_addr, a, sizeof(a));
		net_addr_ntop(AF_INET, &v4->unicast[0].netmask, m, sizeof(m));
		net_addr_ntop(AF_INET, &v4->gw, g, sizeof(g));
		printk("net: address %s mask %s gateway %s mtu %u\n", a, m, g, net_if_get_mtu(iface));
		for (int i = 0; i < DNS_RESOLVER_MAX_SERVERS; i++) {
			if (dns->servers[i].dns_server.sa_family == AF_INET) {
				char s[NET_IPV4_ADDR_LEN];

				net_addr_ntop(AF_INET, &net_sin(&dns->servers[i].dns_server)->sin_addr, s, sizeof(s));
				printk("net: dns server %s\n", s);
			}
		}
	}

	return true;
}

struct snap {
	uint32_t ip_sent, ip_recv, tcp_sent, tcp_recv, tcp_rst, tcp_rexmit, pkts_tx, pkts_rx;
};

static void snap_take(struct snap *s)
{
	struct net_if *iface = net_if_get_first_wifi();

	s->ip_sent = iface->stats.ipv4.sent;
	s->ip_recv = iface->stats.ipv4.recv;
	s->tcp_sent = iface->stats.tcp.sent;
	s->tcp_recv = iface->stats.tcp.recv;
	s->tcp_rst = iface->stats.tcp.rst;
	s->tcp_rexmit = iface->stats.tcp.rexmit;
	s->pkts_tx = iface->stats.ipv4.drop;
	s->pkts_rx = iface->stats.ip_errors.protoerr;
}

static void snap_print(const struct snap *a)
{
	struct snap b;

	snap_take(&b);
	printk("      stack: ip sent %d recv %d, tcp sent %d recv %d rst %d rexmit %d, ip dropped %d, protocol errors %d\n",
	       (int)(b.ip_sent - a->ip_sent), (int)(b.ip_recv - a->ip_recv), (int)(b.tcp_sent - a->tcp_sent),
	       (int)(b.tcp_recv - a->tcp_recv), (int)(b.tcp_rst - a->tcp_rst), (int)(b.tcp_rexmit - a->tcp_rexmit),
	       (int)(b.pkts_tx - a->pkts_tx), (int)(b.pkts_rx - a->pkts_rx));
}

/* one connection attempt: what connect() answers and how long it takes */
static int try_connect(const char *name, const struct sockaddr_in *peer, uint16_t port, bool request,
		       const char *host)
{
	struct sockaddr_in a = *peer;
	struct zsock_timeval tv = {.tv_sec = 6};
	char ip[NET_IPV4_ADDR_LEN];
	int64_t t0 = k_uptime_get();
	int s = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP), ret;
	struct snap before;

	snap_take(&before);

	a.sin_port = htons(port);
	zsock_inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
	if (s < 0) {
		printk("  %-22s %-15s :%-3u socket() failed (%d)\n", name, ip, port, errno);
		return -errno;
	}
	zsock_setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
	zsock_setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	ret = zsock_connect(s, (struct sockaddr *)&a, sizeof(a));
	if (ret < 0) {
		int e = errno;

		printk("  %-22s %-15s :%-3u connect failed: errno %d (%s) after %d ms\n", name, ip, port, e,
		       e == ECONNREFUSED ? "refused, reset by the peer" :
		       e == ETIMEDOUT ? "time out" : e == ECONNRESET ? "reset" : "other",
		       (int)(k_uptime_get() - t0));
		snap_print(&before);
		zsock_close(s);
		k_msleep(40);
		return -e;
	}
	printk("  %-22s %-15s :%-3u connected in %d ms", name, ip, port, (int)(k_uptime_get() - t0));
	if (request) {
		char req[160], rsp[200];
		int n = snprintf(req, sizeof(req),
				 "GET / HTTP/1.1\r\nHost: %s\r\nUser-Agent: probe\r\nConnection: close\r\n\r\n", host);
		int sent = zsock_send(s, req, n, 0);

		if (sent < 0) {
			printk(", request: send failed errno %d", errno);
		} else {
			int r = zsock_recv(s, rsp, sizeof(rsp) - 1, 0);

			if (r < 0) {
				printk(", answer: recv failed errno %d (%s)", errno,
				       errno == ECONNRESET ? "reset" : errno == EAGAIN ? "no answer" : "other");
			} else if (r == 0) {
				printk(", answer: closed without data");
			} else {
				char *e = memchr(rsp, '\r', r);

				if (e != NULL) {
					*e = '\0';
				} else {
					rsp[r] = '\0';
				}
				printk(", answer: %s", rsp);
			}
		}
	}
	printk("\n");
	zsock_close(s);

	return 0;
}

static void probe_host(const char *host)
{
	struct zsock_addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res, *r;
	int64_t t0 = k_uptime_get();
	int ret = zsock_getaddrinfo(host, NULL, &hints, &res), n = 0;

	if (ret != 0) {
		printk("%s: cannot resolve (%d, errno %d) after %d ms\n", host, ret, errno, (int)(k_uptime_get() - t0));
		return;
	}
	printk("%s: resolved in %d ms to", host, (int)(k_uptime_get() - t0));
	for (r = res; r != NULL && n < 6; r = r->ai_next, n++) {
		char ip[NET_IPV4_ADDR_LEN];

		zsock_inet_ntop(AF_INET, &((struct sockaddr_in *)r->ai_addr)->sin_addr, ip, sizeof(ip));
		printk(" %s", ip);
	}
	printk("\n");
	n = 0;
	for (r = res; r != NULL && n < 2; r = r->ai_next, n++) {
		try_connect(host, (struct sockaddr_in *)r->ai_addr, 80, true, host);
		try_connect(host, (struct sockaddr_in *)r->ai_addr, 443, false, host);
	}
	zsock_freeaddrinfo(res);
}

static void probe_ip(const char *name, const char *ip, uint16_t port, bool request)
{
	struct sockaddr_in a = {.sin_family = AF_INET};

	zsock_inet_pton(AF_INET, ip, &a.sin_addr);
	try_connect(name, &a, port, request, name);
}

int main(void)
{
	static const char *const hosts[] = {
		"api.bilibili.com", "www.bilibili.com", "passport.bilibili.com", "i0.hdslb.com",
		"www.baidu.com", "www.qq.com", "example.com",
	};

	printk("\n== net_probe ==\n");
	if (!join()) {
		return 0;
	}
	cap_start(net_if_get_first_wifi());
	k_msleep(1000);
	if (IS_ENABLED(CONFIG_PROBE_SHORT)) {
		printk("\n-- failing address\n");
		probe_ip("a bilibili address", "14.17.92.71", 80, false);
		k_msleep(500);
		printk("\n-- working address\n");
		probe_ip("baidu", "183.2.172.177", 80, false);
		k_msleep(500);
		printk("\n-- failing address again\n");
		probe_ip("a bilibili address", "14.17.92.71", 80, false);
		k_msleep(500);
		return 0;
	}
	for (int round = 1; round <= 1; round++) {
		printk("\n-- round %d, by name --\n", round);
		for (size_t i = 0; i < ARRAY_SIZE(hosts); i++) {
			probe_host(hosts[i]);
		}
		printk("-- round %d, by address --\n", round);
		probe_ip("a bilibili address", "14.17.92.71", 80, true);
		probe_ip("a bilibili address", "14.17.92.71", 443, false);
		probe_ip("cloudflare", "1.1.1.1", 80, true);
		probe_ip("cloudflare", "1.1.1.1", 443, false);
		probe_ip("the PC (proxy)", "192.168.1.9", 7897, false);
		k_msleep(2000);
	}
	printk("\n== done ==\n");

	return 0;
}

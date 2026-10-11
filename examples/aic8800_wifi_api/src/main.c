/*
 * AIC8800 WiFi：通过 Zephyr 的 WiFi 管理接口（net_mgmt）扫描、连接、断开、读状态。
 *
 * 驱动没有 AP 的凭据时不会连接真实网络：CONNECT_SSID 为空串时，连接一段只演示参数检查
 * 和对一个不存在的网络的连接请求，不会联网。
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

/* 要连接的网络，空串表示不连接真实网络 */
#define CONNECT_SSID	""
#define CONNECT_PSK	""

/* 用来演示连接失败的网络名，附近不存在这个网络 */
#define MISSING_SSID	"f101-no-such-network"

#define WIFI_EVENTS (NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE | \
		     NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;
static K_SEM_DEFINE(scan_done, 0, 1);
static K_SEM_DEFINE(connect_done, 0, 1);
static K_SEM_DEFINE(disconnect_done, 0, 1);
static K_SEM_DEFINE(got_ip, 0, 1);
static int connect_status;
static int found;

/* ------------------------------------------------------------------ 事件回调 */

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	switch (event) {
	case NET_EVENT_WIFI_SCAN_RESULT: {
		const struct wifi_scan_result *r = cb->info;
		char bssid[18];

		snprintk(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x", r->mac[0], r->mac[1],
			 r->mac[2], r->mac[3], r->mac[4], r->mac[5]);
		printk("[scan] %2d  ch %3d  rssi %4d  %-8s  %s  \"%.*s\"\n", ++found, r->channel,
		       r->rssi, wifi_security_txt(r->security), bssid, r->ssid_length, r->ssid);
		break;
	}
	case NET_EVENT_WIFI_SCAN_DONE:
		k_sem_give(&scan_done);
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *s = cb->info;

		connect_status = s->status;
		k_sem_give(&connect_done);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		k_sem_give(&disconnect_done);
		break;
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		k_sem_give(&got_ip);
	}
}

/* ------------------------------------------------------------------ net_if_get_first_wifi */

static struct net_if *demo_find_interface(void)
{
	struct net_if *iface = net_if_get_first_wifi();

	if (iface == NULL) {
		printk("[iface] no WiFi interface\n");
		return NULL;
	}
	printk("[iface] net_if_get_first_wifi = index %d, name %s, is up %d\n",
	       net_if_get_by_iface(iface), iface->if_dev->dev->name, net_if_is_up(iface));

	return iface;
}

/* ------------------------------------------------------------------ 本机 MAC 地址 */

static void demo_mac_address(struct net_if *iface)
{
	struct net_linkaddr *addr = net_if_get_link_addr(iface);

	printk("[mac] link address %02x:%02x:%02x:%02x:%02x:%02x, length %u\n", addr->addr[0],
	       addr->addr[1], addr->addr[2], addr->addr[3], addr->addr[4], addr->addr[5],
	       addr->len);
}

/* ------------------------------------------------------------------ 事件回调的注册 */

static void demo_register_events(void)
{
	net_mgmt_init_event_callback(&wifi_cb, wifi_event, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);
	printk("[event] callbacks registered\n");
}

/* ------------------------------------------------------------------ NET_REQUEST_WIFI_IFACE_STATUS */

static void demo_iface_status(struct net_if *iface, const char *when)
{
	struct wifi_iface_status status = {0};
	int ret = net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status));

	if (ret != 0) {
		printk("[status] %s: NET_REQUEST_WIFI_IFACE_STATUS failed: %d\n", when, ret);
		return;
	}
	printk("[status] %s: state %s, mode %s\n", when, wifi_state_txt(status.state),
	       wifi_mode_txt(status.iface_mode));
	if (status.state == WIFI_STATE_COMPLETED) {
		printk("[status] ssid \"%.*s\", channel %u, band %s, rssi %d\n", status.ssid_len,
		       status.ssid, status.channel, wifi_band_txt(status.band), status.rssi);
	}
}

/* ------------------------------------------------------------------ NET_REQUEST_WIFI_SCAN */

static int demo_scan(struct net_if *iface)
{
	int ret;

	found = 0;
	k_sem_reset(&scan_done);
	ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (ret != 0) {
		printk("[scan] NET_REQUEST_WIFI_SCAN failed: %d\n", ret);
		return ret;
	}
	if (k_sem_take(&scan_done, K_SECONDS(30)) != 0) {
		printk("[scan] no scan done event in 30 s\n");
		return -ETIMEDOUT;
	}
	printk("[scan] %d access points\n", found);

	return 0;
}

/* ------------------------------------------------------------------ NET_REQUEST_WIFI_CONNECT */

static int demo_connect(struct net_if *iface, const char *ssid, const char *psk, int wait_s)
{
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)ssid,
		.ssid_length = strlen(ssid),
		.psk = (const uint8_t *)psk,
		.psk_length = strlen(psk),
		.security = strlen(psk) > 0 ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_UNKNOWN,
	};
	int ret;

	k_sem_reset(&connect_done);
	k_sem_reset(&got_ip);
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	if (ret != 0) {
		printk("[connect] \"%s\": NET_REQUEST_WIFI_CONNECT failed: %d\n", ssid, ret);
		return ret;
	}
	printk("[connect] \"%s\": request accepted, waiting up to %d s\n", ssid, wait_s);
	demo_iface_status(iface, "while connecting");
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	printk("[connect] second request while connecting      = %d\n", ret);
	if (k_sem_take(&connect_done, K_SECONDS(wait_s)) != 0) {
		printk("[connect] \"%s\": no result in %d s\n", ssid, wait_s);
		return -ETIMEDOUT;
	}
	if (connect_status != 0) {
		printk("[connect] \"%s\": connect result %d\n", ssid, connect_status);
		return -EIO;
	}
	printk("[connect] \"%s\": connected\n", ssid);
	if (k_sem_take(&got_ip, K_SECONDS(30)) == 0) {
		char buf[NET_IPV4_ADDR_LEN];
		struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;

		printk("[connect] DHCP address %s\n",
		       net_addr_ntop(AF_INET, &ipv4->unicast[0].ipv4.address.in_addr, buf,
				     sizeof(buf)));
	} else {
		printk("[connect] no address from DHCP in 30 s\n");
	}

	return 0;
}

/* 参数检查：这些请求在驱动里被拒绝，不会碰射频 */
static void demo_connect_bad_params(struct net_if *iface)
{
	struct wifi_connect_req_params params = {
		.ssid = (const uint8_t *)"x",
		.ssid_length = 1,
		.psk = (const uint8_t *)"short",
		.psk_length = 5,
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_UNKNOWN,
	};
	int ret;

	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	printk("[connect] passphrase of 5 characters           = %d\n", ret);

	params.psk = (const uint8_t *)"password123";
	params.psk_length = 11;
	params.security = WIFI_SECURITY_TYPE_SAE;
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	printk("[connect] WPA3 (SAE)                           = %d\n", ret);

	params.security = WIFI_SECURITY_TYPE_PSK;
	params.ssid_length = 0;
	ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
	printk("[connect] empty SSID                           = %d\n", ret);
}

/* ------------------------------------------------------------------ NET_REQUEST_WIFI_DISCONNECT */

static int demo_disconnect(struct net_if *iface)
{
	int ret;

	k_sem_reset(&disconnect_done);
	ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
	if (ret != 0) {
		printk("[disconnect] NET_REQUEST_WIFI_DISCONNECT failed: %d\n", ret);
		return ret;
	}
	if (k_sem_take(&disconnect_done, K_SECONDS(5)) == 0) {
		printk("[disconnect] disconnect result event received\n");
	} else {
		printk("[disconnect] request returned 0, no event (nothing was connected)\n");
	}

	return 0;
}

int main(void)
{
	struct net_if *iface;

	printk("AIC8800 WiFi demo\n");
	iface = demo_find_interface();
	if (iface == NULL) {
		return 0;
	}
	demo_mac_address(iface);
	demo_register_events();
	demo_iface_status(iface, "before the scan");

	/* 第一次请求会启动芯片：上电、枚举 SDIO、下载固件 */
	if (demo_scan(iface) != 0) {
		return 0;
	}
	demo_mac_address(iface);
	demo_scan(iface);
	demo_iface_status(iface, "after the scan");

	demo_connect_bad_params(iface);
	if (strlen(CONNECT_SSID) > 0) {
		if (demo_connect(iface, CONNECT_SSID, CONNECT_PSK, 45) == 0) {
			demo_iface_status(iface, "connected");
		}
	} else {
		printk("[connect] CONNECT_SSID is empty, no real network to join\n");
		demo_connect(iface, MISSING_SSID, "password123", 90);
	}
	demo_disconnect(iface);
	demo_iface_status(iface, "after the disconnect");
	printk("done\n");

	return 0;
}

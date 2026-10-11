/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The network of the player: the Wi-Fi link of the AIC8800 chip (scan, join, address by DHCP), the
 * time of day from an NTP server, and the Bluetooth link of the same chip (only the state of the
 * connection is shown). Everything that waits runs in one worker thread; the user interface asks
 * for state and gives commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/fs/fs.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "mp5.h"

#define MAX_APS		24
#define CONNECT_WAIT_S	25
#define DHCP_WAIT_S	20
#define SYNC_RETRIES	3

enum link_state {
	LINK_NONE,		/* no module */
	LINK_STARTING,
	LINK_IDLE,		/* module ready, no network joined */
	LINK_SCANNING,
	LINK_CONNECTING,
	LINK_CONNECTED,		/* joined, waiting for the address */
	LINK_ONLINE,		/* has an address */
	LINK_FAILED,
};

enum command { CMD_SCAN, CMD_CONNECT, CMD_DISCONNECT, CMD_SYNC };

struct request {
	enum command cmd;
	char ssid[33];
	char psk[65];
};

K_MSGQ_DEFINE(requests, sizeof(struct request), 4, 4);
K_THREAD_STACK_DEFINE(net_stack, 6144);
static struct k_thread net_thread;

static struct k_mutex lock;
static volatile enum link_state link = LINK_STARTING;
static char ssid_now[33];
static char addr_now[NET_IPV4_ADDR_LEN];
static char fail_text[40];
static struct net_ap aps[MAX_APS];
static int ap_count;
static char sync_text[48] = "Not synced yet";
static bool sync_pending;

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(scan_done, 0, 1);
static K_SEM_DEFINE(connect_done, 0, 1);
static K_SEM_DEFINE(got_ip, 0, 1);
static int connect_status;

/* ---- events ---------------------------------------------------------------------------- */

static void ap_add(const struct wifi_scan_result *r)
{
	int at = -1;

	k_mutex_lock(&lock, K_FOREVER);
	for (int i = 0; i < ap_count; i++) {
		if (strlen(aps[i].ssid) == r->ssid_length &&
		    memcmp(aps[i].ssid, r->ssid, r->ssid_length) == 0) {
			at = i;
			break;
		}
	}
	if (at < 0 && ap_count < MAX_APS && r->ssid_length > 0) {
		at = ap_count++;
		memcpy(aps[at].ssid, r->ssid, r->ssid_length);
		aps[at].ssid[r->ssid_length] = '\0';
		aps[at].rssi = -128;
	}
	if (at >= 0 && r->rssi > aps[at].rssi) {
		aps[at].rssi = r->rssi;
		aps[at].secured = r->security != WIFI_SECURITY_TYPE_NONE;
	}
	k_mutex_unlock(&lock);
}

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	switch (event) {
	case NET_EVENT_WIFI_SCAN_RESULT:
		ap_add(cb->info);
		break;
	case NET_EVENT_WIFI_SCAN_DONE:
		k_sem_give(&scan_done);
		break;
	case NET_EVENT_WIFI_CONNECT_RESULT:
		connect_status = ((const struct wifi_status *)cb->info)->status;
		k_sem_give(&connect_done);
		break;
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		if (link == LINK_CONNECTED || link == LINK_ONLINE) {
			link = LINK_IDLE;
		}
		break;
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_IPV4_ADDR_ADD) {
		k_mutex_lock(&lock, K_FOREVER);
		net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr,
			      addr_now, sizeof(addr_now));
		k_mutex_unlock(&lock);
		k_sem_give(&got_ip);
	}
}

/* ---- time from the network ------------------------------------------------------------- */

/* days since 1970 to a civil date (proleptic Gregorian) */
static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
	int64_t era, doe, yoe, doy, mp;

	z += 719468;
	era = (z >= 0 ? z : z - 146096) / 146097;
	doe = z - era * 146097;
	yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp < 10 ? mp + 3 : mp - 9;
	*y = yoe + era * 400 + (*m <= 2);
}

static void clock_from_unix(int64_t unix_s)
{
	int64_t local = unix_s + (int64_t)CONFIG_SAMPLE_MP5_UTC_OFFSET_MIN * 60;
	int64_t days = local / 86400;
	int sec = local % 86400, y, m, d;
	struct tm tm = {0};

	civil_from_days(days, &y, &m, &d);
	tm.tm_year = y - 1900;
	tm.tm_mon = m - 1;
	tm.tm_mday = d;
	tm.tm_hour = sec / 3600;
	tm.tm_min = sec / 60 % 60;
	tm.tm_sec = sec % 60;
	{
		struct tm before;

		clock_now(&before);
		printk("net: clock was %04d-%02d-%02d %02d:%02d:%02d, network says %04d-%02d-%02d "
		       "%02d:%02d:%02d\n", before.tm_year + 1900, before.tm_mon + 1, before.tm_mday,
		       before.tm_hour, before.tm_min, before.tm_sec, y, m, d, tm.tm_hour, tm.tm_min,
		       tm.tm_sec);
	}
	clock_set_tm(&tm);
	{
		struct tm after;

		clock_now(&after);
		printk("net: clock now %04d-%02d-%02d %02d:%02d:%02d\n", after.tm_year + 1900,
		       after.tm_mon + 1, after.tm_mday, after.tm_hour, after.tm_min, after.tm_sec);
	}
	k_mutex_lock(&lock, K_FOREVER);
	snprintf(sync_text, sizeof(sync_text), "Synced %04d-%02d-%02d %02d:%02d", y, m, d,
		 tm.tm_hour, tm.tm_min);
	k_mutex_unlock(&lock);
}

static bool time_sync(void)
{
	static const char *const servers[] = {CONFIG_SAMPLE_MP5_NTP_SERVER, "cn.pool.ntp.org",
					      "pool.ntp.org"};
	struct sntp_time ts;

	for (int i = 0; i < ARRAY_SIZE(servers); i++) {
		for (int n = 0; n < SYNC_RETRIES; n++) {
			int ret = sntp_simple(servers[i], 3000, &ts);

			if (ret == 0) {
				printk("net: time from %s: %llu\n", servers[i],
				       (unsigned long long)ts.seconds);
				clock_from_unix((int64_t)ts.seconds);
				return true;
			}
			printk("net: sntp %s: %d\n", servers[i], ret);
			k_msleep(500);
		}
	}
	k_mutex_lock(&lock, K_FOREVER);
	snprintf(sync_text, sizeof(sync_text), "No answer from the time server");
	k_mutex_unlock(&lock);

	return false;
}

/* ---- Wi-Fi commands -------------------------------------------------------------------- */

/* the password of every network that was joined is kept, under a key made from its name */
static void psk_key(const char *ssid, char *key, size_t size)
{
	uint32_t h = 2166136261U;

	for (const char *p = ssid; *p != '\0'; p++) {
		h = (h ^ (uint8_t)*p) * 16777619U;
	}
	snprintf(key, size, "wp%08x", (unsigned int)h);
}

bool app_net_saved_psk(const char *ssid, char *psk, size_t size)
{
	char key[16];
	const char *v;

	psk_key(ssid, key, sizeof(key));
	v = cfg_get(key);
	if (v == NULL) {
		return false;
	}
	snprintf(psk, size, "%s", v);

	return true;
}

static void fail(const char *text)
{
	k_mutex_lock(&lock, K_FOREVER);
	snprintf(fail_text, sizeof(fail_text), "%s", text);
	k_mutex_unlock(&lock);
	link = LINK_FAILED;
}

static void do_scan(struct net_if *iface)
{
	enum link_state before = link;

	k_mutex_lock(&lock, K_FOREVER);
	ap_count = 0;
	k_mutex_unlock(&lock);
	link = LINK_SCANNING;
	k_sem_reset(&scan_done);
	if (net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0) != 0) {
		fail("Scan request failed");
		return;
	}
	if (k_sem_take(&scan_done, K_SECONDS(20)) != 0) {
		printk("net: scan timed out\n");
	}
	link = before == LINK_SCANNING ? LINK_IDLE : before;
}

static void do_connect(struct net_if *iface, const char *ssid, const char *psk)
{
	struct wifi_connect_req_params p = {
		.ssid = ssid,
		.ssid_length = strlen(ssid),
		.psk = psk,
		.psk_length = strlen(psk),
		.security = strlen(psk) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_UNKNOWN,
	};

	if (link == LINK_ONLINE || link == LINK_CONNECTED) {
		/* the same network again is nothing to do; another one replaces the current */
		if (strcmp(ssid_now, ssid) == 0) {
			return;
		}
		net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
		k_msleep(500);
	}
	k_mutex_lock(&lock, K_FOREVER);
	snprintf(ssid_now, sizeof(ssid_now), "%s", ssid);
	k_mutex_unlock(&lock);
	link = LINK_CONNECTING;
	k_sem_reset(&connect_done);
	k_sem_reset(&got_ip);
	if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p)) != 0) {
		fail("Connect request failed");
		return;
	}
	if (k_sem_take(&connect_done, K_SECONDS(CONNECT_WAIT_S)) != 0 || connect_status != 0) {
		net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
		fail(connect_status != 0 ? "Wrong password or no answer" : "No answer from the network");
		if (connect_status != 0) {
			char key[16];

			/* a password that did not work is not kept */
			psk_key(ssid, key, sizeof(key));
			cfg_set(key, NULL);
		}
		return;
	}
	link = LINK_CONNECTED;
	if (k_sem_take(&got_ip, K_SECONDS(DHCP_WAIT_S)) != 0) {
		fail("No address from DHCP");
		return;
	}
	link = LINK_ONLINE;
	{
		char key[16];

		psk_key(ssid, key, sizeof(key));
		cfg_set("wifi_ssid", ssid);
		cfg_set("wifi_psk", psk);
		if (psk[0] != '\0') {
			cfg_set(key, psk);
		}
	}
	time_sync();
}

/* the network on the card: /SD:/wifi.cfg, the name on the first line, the password on the second */
static void wifi_cfg_read(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
	struct fs_file_t f;
	char buf[128];
	ssize_t n;
	char *nl;

	fs_file_t_init(&f);
	if (!storage_ready() || fs_open(&f, "/SD:/wifi.cfg", FS_O_READ) != 0) {
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
	for (char *e = buf + strlen(buf); e > buf && (e[-1] == '\r' || e[-1] == ' '); e--) {
		e[-1] = '\0';
	}
	snprintf(ssid, ssid_size, "%s", buf);
	if (nl != NULL) {
		char *end = strchr(nl, '\n');

		if (end != NULL) {
			*end = '\0';
		}
		for (char *e = nl + strlen(nl); e > nl && (e[-1] == '\r'); e--) {
			e[-1] = '\0';
		}
		snprintf(psk, psk_size, "%s", nl);
	}
	printk("net: network %s from the card\n", ssid);
}

static void net_main(void *a, void *b, void *c)
{
	struct net_if *iface = NULL;
	struct request req;
	char saved_ssid[33] = "", saved_psk[65] = "";
	const char *v;

	/* the chip starts together with the system, the interface shows up when it answers */
	for (int i = 0; i < 100 && iface == NULL; i++) {
		iface = net_if_get_first_wifi();
		if (iface == NULL) {
			k_msleep(100);
		}
	}
	if (iface == NULL) {
		link = LINK_NONE;
		return;
	}
	net_mgmt_init_event_callback(&wifi_cb, wifi_event,
				     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE |
				     NET_EVENT_WIFI_CONNECT_RESULT |
				     NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);
	k_sleep(K_SECONDS(1));
	link = LINK_IDLE;

	/* copies: connecting saves the network again in the same place */
	v = cfg_get("wifi_ssid");
	if (v != NULL) {
		snprintf(saved_ssid, sizeof(saved_ssid), "%s", v);
	}
	v = cfg_get("wifi_psk");
	if (v != NULL) {
		snprintf(saved_psk, sizeof(saved_psk), "%s", v);
	}
	if (saved_ssid[0] == '\0') {
		wifi_cfg_read(saved_ssid, sizeof(saved_ssid), saved_psk, sizeof(saved_psk));
	}
	if (saved_ssid[0] != '\0') {
		printk("net: joining %s\n", saved_ssid);
		do_connect(iface, saved_ssid, saved_psk);
		printk("net: link state %d (%s)\n", (int)link, addr_now);
	}

	for (;;) {
		if (k_msgq_get(&requests, &req, K_FOREVER) != 0) {
			continue;
		}
		switch (req.cmd) {
		case CMD_SCAN:
			do_scan(iface);
			break;
		case CMD_CONNECT:
			do_connect(iface, req.ssid, req.psk);
			break;
		case CMD_DISCONNECT:
			net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
			{
				char key[16];

				/* forgetting the network forgets its password too */
				psk_key(ssid_now, key, sizeof(key));
				cfg_set(key, NULL);
			}
			cfg_set("wifi_ssid", NULL);
			cfg_set("wifi_psk", NULL);
			link = LINK_IDLE;
			break;
		case CMD_SYNC:
			if (link == LINK_ONLINE) {
				time_sync();
			}
			sync_pending = false;
			break;
		}
	}
}

/* ---- Bluetooth ------------------------------------------------------------------------- */

static struct bt_conn *bt_peer;
static char bt_peer_text[BT_ADDR_LE_STR_LEN];
static enum { BT_OFF, BT_STARTING, BT_READY, BT_LINKED, BT_FAILED } bt_state = BT_OFF;

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_start(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data, ARRAY_SIZE(adv_data), NULL, 0);

	if (err != 0 && err != -EALREADY) {
		printk("bt: advertising failed: %d\n", err);
	}
}

static void bt_connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		return;
	}
	if (bt_peer == NULL) {
		bt_peer = bt_conn_ref(conn);
	}
	k_mutex_lock(&lock, K_FOREVER);
	bt_addr_le_to_str(bt_conn_get_dst(conn), bt_peer_text, sizeof(bt_peer_text));
	k_mutex_unlock(&lock);
	bt_state = BT_LINKED;
	printk("bt: connected %s\n", bt_peer_text);
}

static void bt_disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	if (bt_peer != NULL) {
		bt_conn_unref(bt_peer);
		bt_peer = NULL;
	}
	bt_state = BT_READY;
	printk("bt: disconnected (%u)\n", reason);
	adv_start();
}

BT_CONN_CB_DEFINE(bt_conn_cbs) = {
	.connected = bt_connected_cb,
	.disconnected = bt_disconnected_cb,
};

static void bt_main(void *a, void *b, void *c)
{
	int err;

	bt_state = BT_STARTING;
	err = bt_enable(NULL);
	if (err != 0) {
		printk("bt: enable failed: %d\n", err);
		bt_state = BT_FAILED;
		return;
	}
	bt_state = BT_READY;
	adv_start();
}

K_THREAD_STACK_DEFINE(bt_stack, 4096);
static struct k_thread bt_thread;

/* ---- interface for the user interface -------------------------------------------------- */

static void app_net_init(void)
{
	k_mutex_init(&lock);
	k_thread_create(&net_thread, net_stack, K_THREAD_STACK_SIZEOF(net_stack), net_main, NULL, NULL,
			NULL, 12, 0, K_NO_WAIT);
	k_thread_name_set(&net_thread, "net");
	k_thread_create(&bt_thread, bt_stack, K_THREAD_STACK_SIZEOF(bt_stack), bt_main, NULL, NULL,
			NULL, 12, 0, K_MSEC(3500));
	k_thread_name_set(&bt_thread, "bt");
}

static int net_step(void)
{
	app_net_init();

	return 0;
}
MP5_INIT(MP5_STAGE_SERVICES, 10, "network", net_step);

static void put(const struct request *r)
{
	if (k_msgq_put(&requests, r, K_NO_WAIT) != 0) {
		printk("net: busy\n");
	}
}

void app_net_status(char *buf, size_t size)
{
	k_mutex_lock(&lock, K_FOREVER);
	switch (link) {
	case LINK_NONE:
		snprintf(buf, size, "No Wi-Fi module found");
		break;
	case LINK_STARTING:
		snprintf(buf, size, "Starting");
		break;
	case LINK_IDLE:
		snprintf(buf, size, "Not connected");
		break;
	case LINK_SCANNING:
		snprintf(buf, size, "Scanning");
		break;
	case LINK_CONNECTING:
		snprintf(buf, size, "Connecting to %s", ssid_now);
		break;
	case LINK_CONNECTED:
		snprintf(buf, size, "%s, waiting for an address", ssid_now);
		break;
	case LINK_ONLINE:
		snprintf(buf, size, "%s  %s", ssid_now, addr_now);
		break;
	case LINK_FAILED:
		snprintf(buf, size, "%s", fail_text);
		break;
	}
	k_mutex_unlock(&lock);
}

void app_net_sync_status(char *buf, size_t size)
{
	k_mutex_lock(&lock, K_FOREVER);
	if (sync_pending) {
		snprintf(buf, size, "Asking the time server");
	} else if (link != LINK_ONLINE && strncmp(sync_text, "Synced", 6) != 0) {
		snprintf(buf, size, "Needs a Wi-Fi network");
	} else {
		snprintf(buf, size, "%s", sync_text);
	}
	k_mutex_unlock(&lock);
}

void app_net_sync_time(void)
{
	struct request r = {.cmd = CMD_SYNC};

	if (link != LINK_ONLINE) {
		return;
	}
	sync_pending = true;
	put(&r);
}

void bt_status(char *buf, size_t size)
{
	k_mutex_lock(&lock, K_FOREVER);
	switch (bt_state) {
	case BT_OFF:
	case BT_STARTING:
		snprintf(buf, size, "Starting");
		break;
	case BT_READY:
		snprintf(buf, size, "Ready as " CONFIG_BT_DEVICE_NAME);
		break;
	case BT_LINKED:
		snprintf(buf, size, "Connected  %s", bt_peer_text);
		break;
	case BT_FAILED:
		snprintf(buf, size, "Not available");
		break;
	}
	k_mutex_unlock(&lock);
}

bool bt_connected(void)
{
	return bt_state == BT_LINKED;
}

void app_net_scan(void)
{
	struct request r = {.cmd = CMD_SCAN};

	if (link == LINK_NONE || link == LINK_STARTING || link == LINK_SCANNING ||
	    link == LINK_CONNECTING) {
		return;
	}
	put(&r);
}

bool app_net_scanning(void)
{
	return link == LINK_SCANNING;
}

int app_net_scan_results(struct net_ap *out, int max)
{
	int n;

	k_mutex_lock(&lock, K_FOREVER);
	n = MIN(ap_count, max);
	memcpy(out, aps, n * sizeof(*out));
	k_mutex_unlock(&lock);
	/* strongest first */
	for (int i = 1; i < n; i++) {
		struct net_ap t = out[i];
		int j = i;

		while (j > 0 && out[j - 1].rssi < t.rssi) {
			out[j] = out[j - 1];
			j--;
		}
		out[j] = t;
	}

	return n;
}

void app_net_connect(const char *ssid, const char *psk)
{
	struct request r = {.cmd = CMD_CONNECT};

	snprintf(r.ssid, sizeof(r.ssid), "%s", ssid);
	snprintf(r.psk, sizeof(r.psk), "%s", psk);
	put(&r);
}

void app_net_disconnect(void)
{
	struct request r = {.cmd = CMD_DISCONNECT};

	put(&r);
}

bool app_net_connected(void)
{
	return link == LINK_ONLINE;
}

bool app_net_available(void)
{
	return link != LINK_NONE;
}

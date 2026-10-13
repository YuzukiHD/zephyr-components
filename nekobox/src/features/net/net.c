/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The network of the player: the Wi-Fi link of the AIC8800 chip (scan, join, address by DHCP), the
 * time of day from an NTP server. Everything that waits runs in one worker thread; the user interface asks
 * for state and gives commands.
 *
 * The chip is on only while it is used. After the start it is checked (Bluetooth answers, the saved
 * network can be joined, the clock is set from the network) and switched off completely. A scan, a
 * join or a time request switches it on again; when nothing has asked for it for IDLE_OFF_S
 * seconds, and while a game runs, it is off.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "core/cfg.h"
#include "core/clock.h"
#include "core/storage.h"
#include "core/fsutil.h"
#include "features/net/net.h"
#ifdef CONFIG_NEKOBOX_FEATURE_BT
#include "features/net/bt.h"
#endif

#include <aic8800/aic8800.h>
#ifdef CONFIG_NEKOBOX_DEBUG
#include "debug/memcheck.h"
#endif

#define MAX_APS		24
#define CONNECT_WAIT_S	25
#define DHCP_WAIT_S	20
#define SYNC_RETRIES	3
#define IDLE_OFF_S	30

enum link_state {
	LINK_NONE,		/* no module */
	LINK_OFF,		/* the module is off, it is started when it is needed */
	LINK_STARTING,
	LINK_IDLE,		/* module ready, no network joined */
	LINK_SCANNING,
	LINK_CONNECTING,
	LINK_CONNECTED,		/* joined, waiting for the address */
	LINK_ONLINE,		/* has an address */
	LINK_FAILED,
};

enum command { CMD_SCAN, CMD_CONNECT, CMD_DISCONNECT, CMD_SYNC, CMD_PAUSE };

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
static volatile bool net_started;	/* the thread is past its start and takes requests */
static bool radio_up;			/* the chip is running */
static int64_t last_use;		/* uptime of the last request, for the idle switch off */
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
	int64_t local = unix_s + (int64_t)CONFIG_NEKOBOX_UTC_OFFSET_MIN * 60;
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
	static const char *const servers[] = {CONFIG_NEKOBOX_NTP_SERVER, "cn.pool.ntp.org",
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
	char buf[128];
	char *nl;

	if (!storage_ready() || file_read_text("/SD:/wifi.cfg", buf, sizeof(buf)) <= 0) {
		return;
	}
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

/* the saved network: from the settings, else from the card */
static bool saved_network(char *ssid, size_t ssid_size, char *psk, size_t psk_size)
{
	const char *v = cfg_get("wifi_ssid");

	ssid[0] = psk[0] = '\0';
	if (v != NULL) {
		snprintf(ssid, ssid_size, "%s", v);
		v = cfg_get("wifi_psk");
		snprintf(psk, psk_size, "%s", v != NULL ? v : "");
	}
	if (ssid[0] == '\0') {
		wifi_cfg_read(ssid, ssid_size, psk, psk_size);
	}

	return ssid[0] != '\0';
}

/* starts the chip if it is not running; false when it does not answer */
static bool radio_on(void)
{
	if (radio_up) {
		return true;
	}
	link = LINK_STARTING;
	if (aic8800_start() != 0) {
		link = LINK_NONE;
		return false;
	}
	radio_up = true;
	link = LINK_IDLE;
	last_use = k_uptime_get();

	return true;
}

/* leaves the network, stops the driver and powers the chip off: nothing of it stays allocated */
static void radio_off(void)
{
	if (!radio_up) {
		return;
	}
	aic8800_stop();
	radio_up = false;
	k_mutex_lock(&lock, K_FOREVER);
	addr_now[0] = '\0';
	k_mutex_unlock(&lock);
	link = LINK_OFF;
	printk("net: Wi-Fi and Bluetooth are off\n");
#ifdef CONFIG_NEKOBOX_DEBUG
	{
		size_t largest, total;

		nekobox_mem_probe(&largest, &total);
		printk("net: malloc arena now: largest block %u KiB, free in total %u KiB\n",
		       (unsigned int)(largest / 1024), (unsigned int)(total / 1024));
	}
#endif
}

/* joins the saved network (when there is one) so that it is known to work, then back to off */
static void boot_check(struct net_if *iface)
{
	char ssid[33], psk[65];

	if (!radio_on()) {
		return;
	}
#ifdef CONFIG_NEKOBOX_FEATURE_BT
	bt_probe();
#endif
	if (saved_network(ssid, sizeof(ssid), psk, sizeof(psk))) {
		printk("net: joining %s\n", ssid);
		do_connect(iface, ssid, psk);
		printk("net: link state %d (%s)\n", (int)link, addr_now);
	}
	radio_off();
}

static void net_main(void *a, void *b, void *c)
{
	struct net_if *iface = NULL;
	struct request req;

	/* the interface is registered at the start of the system, the chip is not started yet */
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
	link = LINK_OFF;
	/* a game that is running now has the CPU: the chip is not started until it is over */
	nekobox_background_wait();
	net_started = true;
	boot_check(iface);

	for (;;) {
		char ssid[33], psk[65];

		if (k_msgq_get(&requests, &req, K_SECONDS(2)) != 0) {
			if (radio_up && k_uptime_get() - last_use > IDLE_OFF_S * 1000) {
				radio_off();
			}
			continue;
		}
		last_use = k_uptime_get();
		switch (req.cmd) {
		case CMD_SCAN:
			if (radio_on()) {
				do_scan(iface);
			}
			break;
		case CMD_CONNECT:
			if (radio_on()) {
				do_connect(iface, req.ssid, req.psk);
			}
			break;
		case CMD_DISCONNECT:
			if (radio_up) {
				net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
			}
			{
				char key[16];

				/* forgetting the network forgets its password too */
				psk_key(ssid_now, key, sizeof(key));
				cfg_set(key, NULL);
			}
			cfg_set("wifi_ssid", NULL);
			cfg_set("wifi_psk", NULL);
			link = radio_up ? LINK_IDLE : LINK_OFF;
			break;
		case CMD_PAUSE:
			/* nothing uses the network while a game runs */
			radio_off();
			break;
		case CMD_SYNC:
			if (radio_on()) {
				if (link != LINK_ONLINE && saved_network(ssid, sizeof(ssid), psk, sizeof(psk))) {
					do_connect(iface, ssid, psk);
				} else if (link == LINK_ONLINE) {
					time_sync();
				}
			}
			sync_pending = false;
			break;
		}
		last_use = k_uptime_get();
	}
}

/* ---- interface for the user interface -------------------------------------------------- */

static void app_net_init(void)
{
	k_mutex_init(&lock);
	k_thread_create(&net_thread, net_stack, K_THREAD_STACK_SIZEOF(net_stack), net_main, NULL, NULL,
			NULL, 12, 0, K_NO_WAIT);
	k_thread_name_set(&net_thread, "net");
}

static void put(const struct request *r);

static void net_hold(bool hold)
{
	struct request r = {.cmd = CMD_PAUSE};

	/* before the thread has started it waits for the end of the hold by itself; when the hold
	 * ends the chip stays off until something needs it */
	if (hold && net_started) {
		put(&r);
	}
}

static int net_step(void)
{
	nekobox_background_register(net_hold);
	app_net_init();

	return 0;
}
NEKOBOX_INIT(NEKOBOX_STAGE_SERVICES, 10, "network", net_step);

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
	case LINK_OFF:
		snprintf(buf, size, "Off, starts when needed");
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

	if (link == LINK_NONE || link == LINK_STARTING || link == LINK_CONNECTING) {
		return;
	}
	sync_pending = true;
	put(&r);
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

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Shows the screen of an Android phone: joins the WiFi, starts the scrcpy server on the phone
 * through ADB over TCP, decodes its H.264 stream with the video engine and puts the pictures on
 * the video plane. Touches on the panel are sent back to the phone (ui.c).
 */

#include <errno.h>
#include <ff.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/printk.h>
#include <mirror_zephyr/mdns.h>
#include <mirror_zephyr/pairing.h>

#include "app.h"

#define PACKET_MAX	(1024 * 1024)
/* pictures are thrown away while more than this is waiting to be decoded */
#define BACKLOG_DROP	(96 * 1024)
#define REPORT_MS	5000

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

/* ---- WiFi ----------------------------------------------------------------------------- */

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
	} else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		printk("wifi: disconnected\n");
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	printk("wifi: address %s\n",
	       net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr, buf,
			     sizeof(buf)));
	k_sem_give(&got_ip);
}

static bool wifi_join(void)
{
	struct net_if *iface = net_if_get_first_wifi();
	struct wifi_connect_req_params p = {
		.ssid = CONFIG_SAMPLE_MIRROR_WIFI_SSID,
		.ssid_length = strlen(CONFIG_SAMPLE_MIRROR_WIFI_SSID),
		.psk = CONFIG_SAMPLE_MIRROR_WIFI_PSK,
		.psk_length = strlen(CONFIG_SAMPLE_MIRROR_WIFI_PSK),
		.security = WIFI_SECURITY_TYPE_PSK,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_UNKNOWN,
	};

	if (iface == NULL) {
		printk("no WiFi interface\n");
		return false;
	}
	net_mgmt_init_event_callback(&wifi_cb, wifi_event,
				     NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);

	for (int attempt = 0; attempt < 5; attempt++) {
		ui_set_status("joining the WiFi");
		printk("wifi: connecting to %s\n", CONFIG_SAMPLE_MIRROR_WIFI_SSID);
		if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p)) == 0 &&
		    k_sem_take(&connected, K_SECONDS(45)) == 0 &&
		    k_sem_take(&got_ip, K_SECONDS(30)) == 0) {
			return true;
		}
		k_sleep(K_SECONDS(3));
	}

	return false;
}

/* ---- video ---------------------------------------------------------------------------- */

static const struct device *const vdec_dev = DEVICE_DT_GET(DT_NODELABEL(ve));
static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static struct vdec_stream *stream;
static struct vdec_frame shown, older;
static uint32_t shown_count, dropped_count, t_decode_us;

static void release_frames(void)
{
	if (older.priv != NULL) {
		vdec_frame_release(vdec_dev, &older);
	}
	if (shown.priv != NULL) {
		vdec_frame_release(vdec_dev, &shown);
	}
	memset(&older, 0, sizeof(older));
	memset(&shown, 0, sizeof(shown));
}

static int stream_open(void)
{
	const struct vdec_stream_config cfg = {
		.codec = VDEC_CODEC_H264,
		.format = VDEC_FORMAT_NV12,
		.buffer_size = 512 * 1024,
		.no_cache_ops = true,
	};

	return vdec_stream_open(vdec_dev, &cfg, &stream);
}

static void stream_close(void)
{
	if (stream != NULL) {
		release_frames();
		vdec_stream_close(vdec_dev, stream);
		stream = NULL;
	}
}

static void put_up(struct scrcpy *s, struct vdec_frame *f)
{
	struct display_sunxi_yuv yuv = {
		.y = f->plane[0],
		.uv = f->plane[1],
		.width = f->width,
		.height = f->height,
		.stride_y = f->stride[0],
		.stride_uv = f->stride[1],
		.bt709 = true,
		.nonblock = true,
	};

	if (scrcpy_video_backlog(s) > BACKLOG_DROP) {
		vdec_frame_release(vdec_dev, f);
		dropped_count++;

		return;
	}
	display_sunxi_show_yuv(disp, &yuv);
	ui_set_video_size(f->width, f->height);
	/* the picture before the last may still be scanned out */
	if (older.priv != NULL) {
		vdec_frame_release(vdec_dev, &older);
	}
	older = shown;
	shown = *f;
	shown_count++;
}

static int drain(struct scrcpy *s)
{
	struct vdec_frame f;
	int ret;

	while ((ret = vdec_stream_get_frame(vdec_dev, stream, &f)) == 0) {
		put_up(s, &f);
	}

	return ret;
}

static int run_session(uint8_t *pkt, const char *host, uint16_t port, bool tls)
{
	const struct scrcpy_config cfg = {
		.host = host,
		.port = port,
		.tls = tls,
		.key_path = CONFIG_SAMPLE_MIRROR_KEY_FILE,
		.server_file = CONFIG_SAMPLE_MIRROR_SERVER_FILE,
		.version = CONFIG_SAMPLE_MIRROR_SERVER_VERSION,
		.max_size = CONFIG_SAMPLE_MIRROR_MAX_SIZE,
		.max_fps = CONFIG_SAMPLE_MIRROR_MAX_FPS,
		.bit_rate = CONFIG_SAMPLE_MIRROR_BIT_RATE,
		.status = ui_set_status,
	};
	static uint8_t config[512];
	static size_t config_len;
	struct scrcpy s;
	int64_t last_report = k_uptime_get();
	uint32_t report_frames = 0, bytes = 0;

	int started = scrcpy_start(&s, &cfg);

	if (started != 0) {
		return started;
	}
	ui_set_status("waiting for the first picture");
	ui_set_video_size(s.width, s.height);
	ui_set_session(&s);
	config_len = 0;

	while (true) {
		size_t len = PACKET_MAX, off = 0;
		unsigned int flags;
		int64_t pts;
		int ret = scrcpy_read_packet(&s, pkt, &len, &pts, &flags);

		if (ret == -EMSGSIZE) {
			printk("mirror: packet too large, skipped\n");
			continue;
		}
		if (ret != 0) {
			printk("mirror: stream ended: %d\n", ret);
			break;
		}
		bytes += len;

		if (flags & SCRCPY_PACKET_CONFIG) {
			/* a new picture size needs a new decoder */
			if (stream != NULL && (len != config_len || memcmp(config, pkt, len) != 0)) {
				stream_close();
			}
			config_len = MIN(len, sizeof(config));
			memcpy(config, pkt, config_len);
		}
		if (stream == NULL && stream_open() != 0) {
			printk("mirror: cannot open the decoder\n");
			break;
		}

		uint32_t c0 = k_cycle_get_32();

		while (off < len) {
			size_t used = 0;

			ret = vdec_stream_feed(vdec_dev, stream, pkt + off, len - off,
					       (flags & SCRCPY_PACKET_CONFIG) ? -1 : pts, &used);
			if (ret == 0) {
				off += used;
			} else if (ret == -EAGAIN) {
				drain(&s);
				k_msleep(1);
			} else {
				printk("mirror: feed error %d\n", ret);
				break;
			}
		}
		drain(&s);
		t_decode_us += (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - c0);

		if (shown_count != 0U && !(flags & SCRCPY_PACKET_CONFIG)) {
			static bool cleared;

			if (!cleared) {
				ui_set_status("");
				cleared = true;
			}
		}
		report_frames++;
		if (k_uptime_get() - last_report >= REPORT_MS) {
			int ms = (int)(k_uptime_get() - last_report);

			printk("mirror: %u packets (%d.%02d/s), %u shown, %u dropped, %u KB/s, "
			       "decode %u ms per %d ms\n",
			       report_frames, report_frames * 1000 / ms,
			       (report_frames * 100000 / ms) % 100, shown_count, dropped_count,
			       bytes / ms, t_decode_us / 1000, ms);
			report_frames = bytes = t_decode_us = 0;
			last_report = k_uptime_get();
		}
	}
	ui_set_session(NULL);
	stream_close();
	scrcpy_stop(&s);

	return 0;
}

/* ---- finding and pairing the phone ----------------------------------------------------- */

#define SERVICE_CONNECT	"_adb-tls-connect._tcp.local"
#define SERVICE_PAIRING	"_adb-tls-pairing._tcp.local"

static void random_text(char *out, size_t n)
{
	static const char set[] = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
	uint8_t r[32];

	sys_rand_get(r, n);
	for (size_t i = 0; i < n; i++) {
		out[i] = set[r[i] % (sizeof(set) - 1U)];
	}
	out[n] = '\0';
}

/* Shows a QR code for the pairing of wireless debugging and pairs when the phone scans it */
static bool pair_with_phone(void)
{
	char name[16] = "mirror-", pw[12], payload[96], guid[64], host[NET_IPV4_ADDR_LEN];
	struct mdns_service svc;
	int64_t end = k_uptime_get() + 180 * 1000;
	int ret = -ETIMEDOUT;

	random_text(name + 7, 6);
	random_text(pw, 8);
	snprintk(payload, sizeof(payload), "WIFI:T:ADB;S:%s;P:%s;;", name, pw);
	ui_set_status("pair: scan the code with the phone");
	ui_show_qr(payload);
	printk("pairing: waiting for the phone to scan %s\n", payload);

	while (k_uptime_get() < end) {
		if (mdns_find(SERVICE_PAIRING, name, &svc, 5000) != 0) {
			continue;
		}
		zsock_inet_ntop(AF_INET, &svc.addr, host, sizeof(host));
		printk("pairing: phone at %s:%u\n", host, svc.port);
		ui_set_status("pairing");
		ret = adb_pair(host, svc.port, pw, CONFIG_SAMPLE_MIRROR_KEY_FILE, guid, sizeof(guid));
		printk("pairing: result %d, connect service %s\n", ret, ret == 0 ? guid : "-");
		break;
	}
	ui_hide_qr();

	return ret == 0;
}

/* One round: find the phone, connect (pairing first when it does not know us), mirror */
static void connect_round(uint8_t *pkt)
{
	char host[NET_IPV4_ADDR_LEN];
	struct mdns_service svc;
	int ret;

	if (strlen(CONFIG_SAMPLE_MIRROR_PHONE_IP) > 0U) {
		/* a fixed address: ADB over TCP (adb tcpip 5555) */
		run_session(pkt, CONFIG_SAMPLE_MIRROR_PHONE_IP, CONFIG_SAMPLE_MIRROR_PHONE_PORT,
			    false);
		return;
	}
	ui_set_status("looking for the phone (turn on Wireless debugging)");
	if (mdns_find(SERVICE_CONNECT, NULL, &svc, 6000) != 0) {
		/* nothing known to connect to: offer the code, the phone then pairs and shows up */
		printk("mirror: no wireless debugging service found, showing the pairing code\n");
		pair_with_phone();
		return;
	}
	zsock_inet_ntop(AF_INET, &svc.addr, host, sizeof(host));
	printk("mirror: %s at %s:%u\n", svc.instance, host, svc.port);
	ret = run_session(pkt, host, svc.port, true);
	if (ret == -EACCES && pair_with_phone()) {
		/* the connect port is new after a pairing */
		k_sleep(K_SECONDS(1));
	}
}

int main(void)
{
	uint8_t *pkt;
	int ret;

	ui_start();
	if (!device_is_ready(vdec_dev) || !device_is_ready(disp)) {
		printk("a device is not ready\n");
		return 0;
	}
	for (int attempt = 0; attempt < 5; attempt++) {
		ret = fs_mount(&mp);
		if (ret == 0) {
			break;
		}
		printk("mount attempt %d failed: %d\n", attempt + 1, ret);
		k_sleep(K_MSEC(1000));
	}
	if (ret != 0) {
		printk("cannot mount the SD card: %d\n", ret);
		ui_set_status("no SD card");
		return 0;
	}
	pkt = malloc(PACKET_MAX);
	if (pkt == NULL) {
		return 0;
	}
	if (!wifi_join()) {
		ui_set_status("no WiFi");
		return 0;
	}

	while (true) {
		connect_round(pkt);
		ui_set_status("connection lost, retrying");
		k_sleep(K_SECONDS(3));
	}

	return 0;
}

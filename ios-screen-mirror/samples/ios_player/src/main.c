/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Shows the screen of an iPhone: joins the WiFi, announces itself as an AirPlay receiver and
 * decodes the H.264 stream of the mirroring phone with the video engine onto the video plane.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <ff.h>
#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/vdec.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/igmp.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>
#include <ios_zephyr/airplay.h>

#define REPORT_MS	5000

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

/* ---- status text -------------------------------------------------------------------- */

static lv_obj_t *status_label;
static K_MUTEX_DEFINE(ui_lock);

static void status(const char *text)
{
	printk("ios: %s\n", text);
	k_mutex_lock(&ui_lock, K_FOREVER);
	if (text[0] == '\0') {
		lv_obj_add_flag(lv_obj_get_parent(status_label), LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_label_set_text(status_label, text);
		lv_obj_remove_flag(lv_obj_get_parent(status_label), LV_OBJ_FLAG_HIDDEN);
	}
	k_mutex_unlock(&ui_lock);
}

static void ui_build(void)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *card = lv_obj_create(scr);

	lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
	lv_obj_set_size(card, 640, 160);
	lv_obj_center(card);
	lv_obj_set_style_bg_color(card, lv_color_make(250, 250, 252), 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(card, 24, 0);
	lv_obj_set_style_border_width(card, 0, 0);
	status_label = lv_label_create(card);
	lv_obj_set_style_text_font(status_label, &lv_font_montserrat_28, 0);
	lv_obj_set_style_text_color(status_label, lv_color_make(30, 32, 40), 0);
	lv_obj_set_style_bg_color(status_label, lv_color_make(250, 250, 252), 0);
	lv_obj_set_style_bg_opa(status_label, LV_OPA_COVER, 0);
	lv_label_set_text(status_label, "");
	lv_obj_center(status_label);
}

/* ---- WiFi ---------------------------------------------------------------------------- */

static struct net_mgmt_event_callback wifi_cb, ipv4_cb;
static K_SEM_DEFINE(connect_done, 0, 1);
static K_SEM_DEFINE(got_ip, 0, 1);
static volatile int connect_status;

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *s = cb->info;

		connect_status = s->status;
		k_sem_give(&connect_done);
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t event, struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	printk("ios: address %s\n",
	       net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr, buf,
			     sizeof(buf)));
	k_sem_give(&got_ip);
}

/* The network of the Android mirror player: the name on the first line, the password next */
static void wifi_load(char *ssid, char *psk)
{
	struct fs_file_t f;
	char buf[128] = {0};
	ssize_t n = 0;

	ssid[0] = psk[0] = '\0';
	fs_file_t_init(&f);
	if (fs_open(&f, "/SD:/wifi.cfg", FS_O_READ) == 0) {
		n = fs_read(&f, buf, sizeof(buf) - 1);
		fs_close(&f);
	}
	if (n > 0) {
		char *nl = strchr(buf, '\n');

		if (nl != NULL) {
			*nl = '\0';
			strncpy(ssid, buf, 32);
			strncpy(psk, nl + 1, 64);
			nl = strchr(psk, '\n');
			if (nl != NULL) {
				*nl = '\0';
			}
		}
	}
	if (ssid[0] == '\0') {
		strncpy(ssid, CONFIG_SAMPLE_IOS_WIFI_SSID, 32);
		strncpy(psk, CONFIG_SAMPLE_IOS_WIFI_PSK, 64);
	}
}

static bool wifi_join(struct net_if *iface)
{
	char ssid[33] = {0}, psk[65] = {0};
	struct wifi_connect_req_params p = {
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_UNKNOWN,
	};

	wifi_load(ssid, psk);
	if (ssid[0] == '\0') {
		status("No WiFi: put wifi.cfg on the card");
		return false;
	}
	p.ssid = ssid;
	p.ssid_length = strlen(ssid);
	p.psk = psk;
	p.psk_length = strlen(psk);
	p.security = p.psk_length > 0U ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;

	status("Joining the WiFi");
	net_mgmt_init_event_callback(&wifi_cb, wifi_event, NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);
	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&ipv4_cb);
	connect_status = -1;
	if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p)) != 0 ||
	    k_sem_take(&connect_done, K_SECONDS(30)) != 0 || connect_status != 0) {
		status("Cannot join the WiFi");
		return false;
	}

	return k_sem_take(&got_ip, K_SECONDS(30)) == 0;
}

/* ---- video ---------------------------------------------------------------------------- */

static const struct device *const vdec_dev = DEVICE_DT_GET(DT_NODELABEL(ve));
static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static struct vdec_stream *stream;
static struct vdec_frame shown, older;
static uint32_t shown_count, packets, bytes, t_decode_us;
static int64_t last_report;
static K_MUTEX_DEFINE(video_lock);
static bool too_big;


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
		/* the phone's stream keeps several reference pictures; the pictures are small */
		.holding_frames = CONFIG_SAMPLE_IOS_HOLD_FRAMES,
	};

	return vdec_stream_open(vdec_dev, &cfg, &stream);
}

static void stream_close(void)
{
	if (stream != NULL) {
		/* the pictures must not be on the screen when they are given back */
		display_sunxi_hide_yuv(disp);
		release_frames();
		vdec_stream_close(vdec_dev, stream);
		stream = NULL;
	}
	/* the next stream shows its first picture by taking the status text down */
	shown_count = 0;
}

static void drain(void)
{
	struct vdec_frame f;
	int ret;

	while ((ret = vdec_stream_get_frame(vdec_dev, stream, &f)) == 0) {
		struct display_sunxi_yuv yuv = {
			.y = f.plane[0],
			.uv = f.plane[1],
			.width = f.width,
			.height = f.height,
			.stride_y = f.stride[0],
			.stride_uv = f.stride[1],
			.bt709 = true,
			.nonblock = true,
		};

		display_sunxi_show_yuv(disp, &yuv);
		/* the picture before the last may still be scanned out */
		if (older.priv != NULL) {
			vdec_frame_release(vdec_dev, &older);
		}
		older = shown;
		shown = f;
		if (shown_count++ == 0U) {
			printk("ios: first picture %ux%u\n", f.width, f.height);
			status("");
		}
	}
	if (ret == -EBUSY) {
		static int64_t last;

		if (k_uptime_get() - last > 1000) {
			printk("ios: decoder has no free picture buffer\n");
			last = k_uptime_get();
		}
	}
}


/* ---- what the SPS says ---------------------------------------------------------------- */

struct bits {
	const uint8_t *p;
	size_t n, pos;
};

static uint32_t get_bits(struct bits *b, unsigned int count)
{
	uint32_t v = 0;

	while (count-- > 0U) {
		size_t byte = b->pos >> 3;

		v = (v << 1) | (byte < b->n ? (b->p[byte] >> (7 - (b->pos & 7U))) & 1U : 0U);
		b->pos++;
	}

	return v;
}

static uint32_t get_ue(struct bits *b)
{
	unsigned int zeros = 0;

	while (get_bits(b, 1) == 0U && zeros < 32U) {
		zeros++;
	}

	return ((1U << zeros) - 1U) + get_bits(b, zeros);
}

/* logs the size and the number of reference pictures of the first SPS in an Annex B buffer */
static bool read_sps(const uint8_t *d, size_t len, unsigned int *width, unsigned int *height,
		     unsigned int *refs_out)
{
	size_t i = 0;

	while (i + 5 < len && !(d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1 && (d[i + 3] & 0x1f) == 7)) {
		i++;
	}
	if (i + 5 >= len) {
		return false;
	}

	uint8_t rbsp[64];
	size_t n = MIN(len - (i + 4), sizeof(rbsp)), o = 0;

	for (size_t k = 0; k < n; k++) {
		const uint8_t c = d[i + 4 + k];

		if (o >= 2 && rbsp[o - 1] == 0 && rbsp[o - 2] == 0 && c == 3) {
			continue;	/* emulation prevention byte */
		}
		rbsp[o++] = c;
	}

	struct bits b = {.p = rbsp, .n = o, .pos = 0};
	unsigned int profile = get_bits(&b, 8);
	unsigned int level;
	uint32_t refs, w_mbs, h_mbs, frame_mbs_only;

	get_bits(&b, 8);
	level = get_bits(&b, 8);
	get_ue(&b);
	if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44) {
		uint32_t chroma = get_ue(&b);

		if (chroma == 3U) {
			get_bits(&b, 1);
		}
		get_ue(&b);
		get_ue(&b);
		get_bits(&b, 1);
		if (get_bits(&b, 1)) {
			printk("ios: SPS with scaling lists, size not read\n");
			return false;
		}
	}
	get_ue(&b);			/* log2_max_frame_num - 4 */
	if (get_ue(&b) == 0U) {		/* pic_order_cnt_type */
		get_ue(&b);
	}
	refs = get_ue(&b);
	get_bits(&b, 1);
	w_mbs = get_ue(&b) + 1U;
	h_mbs = get_ue(&b) + 1U;
	frame_mbs_only = get_bits(&b, 1);
	*width = w_mbs * 16U;
	*height = h_mbs * 16U * (frame_mbs_only ? 1U : 2U);
	*refs_out = refs;
	printk("ios: stream profile %u level %u, %u reference frames, %ux%u\n", profile, level, refs,
	       *width, *height);

	return true;
}

static void on_video(const uint8_t *data, size_t size, uint64_t pts_us, bool config, void *arg)
{
	size_t off = 0;
	uint32_t c0;

	k_mutex_lock(&video_lock, K_FOREVER);
	if (config) {
		unsigned int w, h, refs;

		too_big = false;
		if (read_sps(data, size, &w, &h, &refs)) {
			/* the engine allocates two pictures plus the held ones; one more for safety */
			size_t need = (size_t)w * h * 3 / 2 * (2U + CONFIG_SAMPLE_IOS_HOLD_FRAMES);

			if (need > (CONFIG_VDEC_SUNXI_MAX_MEMORY_KB - 512U) * 1024U) {
				printk("ios: %ux%u does not fit the decoder (%zu KB)\n", w, h,
				       need / 1024U);
				too_big = true;
			}
		}
		if (too_big) {
			char text[64];

			stream_close();
			snprintk(text, sizeof(text), "Unsupported resolution %ux%u", w, h);
			status(text);
		}
	}
	if (too_big) {
		goto out;
	}
	if (config && stream != NULL) {
		/* a new SPS: the picture size may have changed */
		stream_close();
	}
	if (stream == NULL && stream_open() != 0) {
		printk("ios: cannot open the decoder\n");
		goto out;
	}
	c0 = k_cycle_get_32();
	int stuck = 0;

	while (off < size) {
		size_t used = 0;
		int ret = vdec_stream_feed(vdec_dev, stream, data + off, size - off,
					   config ? -1 : (int64_t)pts_us, &used);

		if (ret == 0) {
			off += used;
		} else if (ret == -EAGAIN) {
			drain();
			k_msleep(1);
			if (++stuck % 1000 == 0) {
				printk("ios: decoder not taking data (%d ms), %zu of %zu fed, shown %u\n",
				       stuck, off, size, shown_count);
			}
		} else {
			printk("ios: feed error %d\n", ret);
			break;
		}
	}
	drain();
	t_decode_us += (uint32_t)k_cyc_to_us_floor64(k_cycle_get_32() - c0);
	packets++;
	bytes += size;
	if (k_uptime_get() - last_report >= REPORT_MS) {
		int ms = (int)(k_uptime_get() - last_report);

		printk("ios: %u packets (%u/s), %u shown, %u KB/s, decode %u ms per %d ms\n",
		       packets, packets * 1000 / ms, shown_count, bytes / ms,
		       t_decode_us / 1000, ms);
		packets = bytes = t_decode_us = 0;
		last_report = k_uptime_get();
	}
out:
	k_mutex_unlock(&video_lock);
}

static void on_connection(bool up, void *arg)
{
	printk("ios: phone %s\n", up ? "connected" : "disconnected");
	if (!up) {
		k_mutex_lock(&video_lock, K_FOREVER);
		stream_close();
		shown_count = 0;
		k_mutex_unlock(&video_lock);
		status("Waiting for the phone");
	}
}

static void on_flush(void *arg)
{
	k_mutex_lock(&video_lock, K_FOREVER);
	stream_close();
	shown_count = 0;
	k_mutex_unlock(&video_lock);
}

int main(void)
{
	struct airplay_config cfg = {
		.name = CONFIG_IOS_MIRROR_NAME,
		.port = CONFIG_IOS_MIRROR_PORT,
		.video = on_video,
		.connection = on_connection,
		.flush = on_flush,
	};
	struct net_if *iface = net_if_get_first_wifi();
	int ret;

	ui_build();
	if (!device_is_ready(vdec_dev) || !device_is_ready(disp) || iface == NULL) {
		printk("a device is not ready\n");
		return 0;
	}
	for (int attempt = 0; attempt < 5; attempt++) {
		ret = fs_mount(&mp);
		if (ret == 0) {
			break;
		}
		k_sleep(K_MSEC(1000));
	}
	if (ret != 0) {
		printk("cannot mount the SD card: %d\n", ret);
	}
	if (!wifi_join(iface)) {
		return 0;
	}
	/* the responder joins the group when an interface comes up, which was before it started */
	{
		struct in_addr mdns_group = {{{224, 0, 0, 251}}};

		ret = net_ipv4_igmp_join(iface, &mdns_group, NULL);
		printk("ios: mDNS group joined: %d\n", ret);
	}
	memcpy(cfg.hw_addr, net_if_get_link_addr(iface)->addr, sizeof(cfg.hw_addr));
	ret = airplay_start(&cfg);
	if (ret != 0) {
		printk("ios: cannot start AirPlay: %d\n", ret);
		status("Cannot start AirPlay");
		return 0;
	}
	last_report = k_uptime_get();
	{
		char text[96];

		snprintk(text, sizeof(text), "Screen Mirroring: choose \"%s\"", CONFIG_IOS_MIRROR_NAME);
		status(text);
	}
	while (true) {
		lv_timer_handler();
		k_msleep(50);
	}
}

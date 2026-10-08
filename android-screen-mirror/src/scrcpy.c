/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The server runs on the phone (app_process) and listens on an abstract local socket; the
 * client opens it twice through ADB: the first stream carries the video (a dummy byte, the
 * device name, the codec, then packets of a 12 byte header and the data), the second one the
 * control messages. Messages the server sends on the control stream (clipboard) are dropped.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <mirror_zephyr/scrcpy.h>

#define REMOTE_JAR	"/data/local/tmp/scrcpy-server.jar"

#define CTRL_INJECT_KEYCODE	0
#define CTRL_INJECT_TOUCH	2

#define PACKET_FLAG_CONFIG	BIT64(63)
#define PACKET_FLAG_KEY_FRAME	BIT64(62)

#define SOCKET_RETRIES		60
#define SOCKET_RETRY_MS		250

static void say(const struct scrcpy_config *cfg, const char *text)
{
	printk("scrcpy: %s\n", text);
	if (cfg->status != NULL) {
		cfg->status(text);
	}
}

/* Output of the server process: shown, so that a failing start can be understood */
static void shell_sink(const uint8_t *data, size_t len, void *arg)
{
	ARG_UNUSED(arg);
	printk("server: %.*s", (int)MIN(len, 200U), (const char *)data);
}

static void drop_sink(const uint8_t *data, size_t len, void *arg)
{
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(arg);
}

int scrcpy_start(struct scrcpy *s, const struct scrcpy_config *cfg)
{
	char cmd[512], sock[48];
	uint8_t meta[12];
	uint32_t scid;
	bool pushed;
	int ret;

	memset(s, 0, sizeof(*s));
	say(cfg, "connecting to the phone");
	ret = cfg->tls ? adb_connect_tls(&s->adb, cfg->host, cfg->port, cfg->key_path)
		       : adb_connect(&s->adb, cfg->host, cfg->port, cfg->key_path, 60000);
	if (ret != 0) {
		printk("scrcpy: adb connect failed: %d\n", ret);
		say(cfg, ret == -EACCES ? "key refused by the phone" : "cannot connect to adb");
		return ret;
	}
	if (cfg->server_file != NULL) {
		say(cfg, "copying the server");
		ret = adb_push_file(s->adb, cfg->server_file, REMOTE_JAR, 0644, &pushed);
		if (ret != 0) {
			printk("scrcpy: push failed: %d\n", ret);
			say(cfg, "cannot copy the server");
			goto fail;
		}
		printk("scrcpy: server %s\n", pushed ? "copied" : "already on the phone");
	}

	scid = sys_rand32_get() & 0x7fffffffU;
	snprintk(sock, sizeof(sock), "localabstract:scrcpy_%08x", scid);
	snprintk(cmd, sizeof(cmd),
		 "shell:CLASSPATH=%s app_process / com.genymobile.scrcpy.Server %s scid=%08x "
		 "log_level=info video=true audio=false control=true tunnel_forward=true "
		 "max_size=%u max_fps=%u video_bit_rate=%u video_codec=h264 "
		 "send_device_meta=true send_frame_meta=true send_dummy_byte=true "
		 "send_codec_meta=true clipboard_autosync=false",
		 REMOTE_JAR, cfg->version, scid, cfg->max_size, cfg->max_fps, cfg->bit_rate);
	say(cfg, "starting the server");
	ret = adb_open(s->adb, cmd, shell_sink, NULL, &s->shell);
	if (ret != 0) {
		printk("scrcpy: cannot start the server: %d\n", ret);
		goto fail;
	}

	/* the server needs a moment before it listens */
	ret = -ECONNREFUSED;
	for (int i = 0; i < SOCKET_RETRIES && ret != 0; i++) {
		if (adb_is_closed(s->shell)) {
			ret = -ECONNRESET;
			break;
		}
		k_msleep(SOCKET_RETRY_MS);
		ret = adb_open(s->adb, sock, NULL, NULL, &s->video);
	}
	if (ret != 0) {
		printk("scrcpy: the server did not come up: %d\n", ret);
		say(cfg, "the server did not start");
		goto fail;
	}
	ret = adb_open(s->adb, sock, drop_sink, NULL, &s->control);
	if (ret != 0) {
		printk("scrcpy: no control socket: %d\n", ret);
		goto fail;
	}

	/* dummy byte, device name, codec id + size */
	ret = adb_read(s->video, meta, 1, K_SECONDS(10));
	if (ret == 0) {
		ret = adb_read(s->video, s->device_name, 64, K_SECONDS(5));
	}
	if (ret == 0) {
		ret = adb_read(s->video, meta, 12, K_SECONDS(5));
	}
	if (ret != 0) {
		printk("scrcpy: no stream header: %d\n", ret);
		goto fail;
	}
	s->device_name[64] = '\0';
	s->width = sys_get_be32(meta + 4);
	s->height = sys_get_be32(meta + 8);
	printk("scrcpy: %s, %ux%u, codec %.4s\n", s->device_name, s->width, s->height,
	       (const char *)meta);

	return 0;
fail:
	scrcpy_stop(s);

	return ret < 0 ? ret : -EIO;
}

void scrcpy_abort(struct scrcpy *s)
{
	adb_abort(s->adb);
}

void scrcpy_stop(struct scrcpy *s)
{
	adb_close(s->control);
	adb_close(s->video);
	adb_close(s->shell);
	adb_disconnect(s->adb);
	memset(s, 0, sizeof(*s));
}

int scrcpy_read_packet(struct scrcpy *s, uint8_t *buf, size_t *size, int64_t *pts_us,
		       unsigned int *flags)
{
	uint8_t h[12];
	uint64_t pts;
	uint32_t len;
	int ret;

	ret = adb_read(s->video, h, sizeof(h), K_FOREVER);
	if (ret != 0) {
		return ret;
	}
	pts = sys_get_be64(h);
	len = sys_get_be32(h + 8);
	*flags = ((pts & PACKET_FLAG_CONFIG) ? SCRCPY_PACKET_CONFIG : 0) |
		 ((pts & PACKET_FLAG_KEY_FRAME) ? SCRCPY_PACKET_KEY : 0);
	*pts_us = (int64_t)(pts & ~(PACKET_FLAG_CONFIG | PACKET_FLAG_KEY_FRAME));
	if (len > *size) {
		uint8_t skip[256];

		/* a packet that does not fit is thrown away */
		while (len > 0U) {
			uint32_t n = MIN(len, (uint32_t)sizeof(skip));

			ret = adb_read(s->video, skip, n, K_SECONDS(5));
			if (ret != 0) {
				return ret;
			}
			len -= n;
		}

		return -EMSGSIZE;
	}
	ret = adb_read(s->video, buf, len, K_SECONDS(5));
	*size = len;

	return ret;
}

size_t scrcpy_video_backlog(struct scrcpy *s)
{
	return adb_available(s->video);
}

int scrcpy_send_touch(struct scrcpy *s, const struct scrcpy_touch *t, unsigned int count,
		      uint16_t w, uint16_t h)
{
	uint8_t msg[5 * 32];
	uint8_t *p = msg;

	count = MIN(count, 5U);
	for (unsigned int i = 0; i < count; i++) {
		p[0] = CTRL_INJECT_TOUCH;
		p[1] = t[i].action;
		sys_put_be64(t[i].id, p + 2);
		sys_put_be32(t[i].x, p + 10);
		sys_put_be32(t[i].y, p + 14);
		sys_put_be16(w, p + 18);
		sys_put_be16(h, p + 20);
		sys_put_be16(t[i].action == SCRCPY_TOUCH_UP ? 0 : 0xffff, p + 22);
		sys_put_be32(0, p + 24);	/* action button */
		sys_put_be32(0, p + 28);	/* buttons */
		p += 32;
	}

	return adb_write(s->control, msg, p - msg);
}

int scrcpy_send_key(struct scrcpy *s, bool down, uint32_t keycode)
{
	uint8_t msg[14];

	msg[0] = CTRL_INJECT_KEYCODE;
	msg[1] = down ? 0 : 1;
	sys_put_be32(keycode, msg + 2);
	sys_put_be32(0, msg + 6);	/* repeat */
	sys_put_be32(0, msg + 10);	/* meta state */

	return adb_write(s->control, msg, sizeof(msg));
}

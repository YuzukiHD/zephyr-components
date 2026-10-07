/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A minimal iperf3 client for one TCP stream.
 *
 * The control connection carries single byte states and two JSON documents: the client
 * sends its cookie, the server asks for the parameters (PARAM_EXCHANGE), tells the client to open
 * the data stream (CREATE_STREAMS) and starts the test (TEST_START, TEST_RUNNING). The sender of
 * the client ends the test after the time with TEST_END, in both directions, and the results are
 * exchanged before the server says DISPLAY_RESULTS and the client IPERF_DONE.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>

#include "iperf3.h"

#define COOKIE_SIZE	37
#define BLOCK_SIZE	4096

enum {
	TEST_START = 1,
	TEST_RUNNING = 2,
	TEST_END = 4,
	PARAM_EXCHANGE = 9,
	CREATE_STREAMS = 10,
	EXCHANGE_RESULTS = 13,
	DISPLAY_RESULTS = 14,
	IPERF_DONE = 16,
};

static uint8_t block[BLOCK_SIZE];

static int wait_readable(int fd, int timeout_ms)
{
	struct zsock_pollfd p = { .fd = fd, .events = ZSOCK_POLLIN };
	int ret = zsock_poll(&p, 1, timeout_ms);

	return ret > 0 ? 0 : (ret == 0 ? -ETIMEDOUT : -errno);
}

static int read_full(int fd, void *buf, size_t len, int timeout_ms)
{
	uint8_t *p = buf;

	while (len > 0) {
		int ret = wait_readable(fd, timeout_ms);
		ssize_t n;

		if (ret < 0) {
			return ret;
		}
		n = zsock_recv(fd, p, len, 0);
		if (n <= 0) {
			return n == 0 ? -ECONNRESET : -errno;
		}
		p += n;
		len -= n;
	}
	return 0;
}

static int write_full(int fd, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len > 0) {
		ssize_t n = zsock_send(fd, p, len, 0);

		if (n < 0) {
			return -errno;
		}
		p += n;
		len -= n;
	}
	return 0;
}

static int read_state(int fd, int expect, int timeout_ms)
{
	int8_t state;
	int ret = read_full(fd, &state, 1, timeout_ms);

	if (ret < 0) {
		return ret;
	}
	if (state != expect) {
		printk("iperf3: state %d, expected %d\n", state, expect);
		return -EPROTO;
	}
	return 0;
}

static int write_state(int fd, int state)
{
	int8_t s = state;

	return write_full(fd, &s, 1);
}

static int write_json(int fd, const char *json)
{
	uint32_t len = sys_cpu_to_be32(strlen(json));
	int ret = write_full(fd, &len, sizeof(len));

	return ret < 0 ? ret : write_full(fd, json, strlen(json));
}

/* the document of the server is only read, its content does not matter here */
static int skip_json(int fd)
{
	uint32_t len;
	char tmp[64];
	int ret = read_full(fd, &len, sizeof(len), 5000);

	if (ret < 0) {
		return ret;
	}
	len = sys_be32_to_cpu(len);
	while (len > 0) {
		size_t n = MIN(len, sizeof(tmp));

		ret = read_full(fd, tmp, n, 5000);
		if (ret < 0) {
			return ret;
		}
		len -= n;
	}
	return 0;
}

static int open_stream(const struct sockaddr_in *addr, const char *cookie)
{
	int fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (fd < 0) {
		return -errno;
	}
	if (zsock_connect(fd, (const struct sockaddr *)addr, sizeof(*addr)) < 0) {
		int err = -errno;

		zsock_close(fd);
		return err;
	}
	if (write_full(fd, cookie, COOKIE_SIZE) < 0) {
		zsock_close(fd);
		return -EIO;
	}
	return fd;
}

int iperf3_tcp(const char *server, uint16_t port, int seconds, bool reverse, uint64_t *bytes)
{
	static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
	struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(port) };
	char cookie[COOKIE_SIZE];
	char json[256];
	int ctrl = -1, data = -1, ret;
	int64_t t0, last, end;
	uint64_t total = 0, last_total = 0;

	if (zsock_inet_pton(AF_INET, server, &addr.sin_addr) != 1) {
		return -EINVAL;
	}
	for (int i = 0; i < COOKIE_SIZE - 1; i++) {
		cookie[i] = alphabet[sys_rand32_get() % (sizeof(alphabet) - 1)];
	}
	cookie[COOKIE_SIZE - 1] = '\0';
	memset(block, 'x', sizeof(block));

	ctrl = open_stream(&addr, cookie);
	if (ctrl < 0) {
		printk("iperf3: no connection to %s:%d: %d\n", server, port, ctrl);
		return ctrl;
	}

	ret = read_state(ctrl, PARAM_EXCHANGE, 5000);
	if (ret < 0) {
		goto out;
	}
	snprintf(json, sizeof(json),
		 "{\"tcp\":true,\"omit\":0,\"time\":%d,\"parallel\":1,\"len\":%d,"
		 "\"client_version\":\"3.20\"%s}", seconds, BLOCK_SIZE,
		 reverse ? ",\"reverse\":true" : "");
	ret = write_json(ctrl, json);
	if (ret < 0) {
		goto out;
	}

	ret = read_state(ctrl, CREATE_STREAMS, 5000);
	if (ret < 0) {
		goto out;
	}
	data = open_stream(&addr, cookie);
	if (data < 0) {
		ret = data;
		goto out;
	}
	ret = read_state(ctrl, TEST_START, 5000);
	if (ret == 0) {
		ret = read_state(ctrl, TEST_RUNNING, 5000);
	}
	if (ret < 0) {
		goto out;
	}

	printk("iperf3: %s %d s\n", reverse ? "download" : "upload", seconds);
	t0 = k_uptime_get();
	last = t0;
	end = t0 + (int64_t)seconds * 1000;

	if (!reverse) {
		while (k_uptime_get() < end) {
			ret = write_full(data, block, sizeof(block));
			if (ret < 0) {
				goto out;
			}
			total += sizeof(block);
			if (k_uptime_get() - last >= 1000) {
				int64_t now = k_uptime_get();

				printk("  %2d s  %5u kbit/s\n", (int)((now - t0) / 1000),
				       (unsigned int)((total - last_total) * 8 / (now - last)));
				last = now;
				last_total = total;
			}
		}
		ret = write_state(ctrl, TEST_END);
	} else {
		struct zsock_pollfd p = { .fd = data, .events = ZSOCK_POLLIN };

		/* the client times the test in both directions and ends it */
		while (k_uptime_get() < end) {
			ret = zsock_poll(&p, 1, 1000);
			if (ret < 0) {
				ret = -errno;
				goto out;
			}
			if (ret > 0 && (p.revents & ZSOCK_POLLIN)) {
				ssize_t n = zsock_recv(data, block, sizeof(block), 0);

				if (n > 0) {
					total += n;
				}
			}
			if (k_uptime_get() - last >= 1000) {
				int64_t now = k_uptime_get();

				printk("  %2d s  %5u kbit/s\n", (int)((now - t0) / 1000),
				       (unsigned int)((total - last_total) * 8 / (now - last)));
				last = now;
				last_total = total;
			}
		}
		ret = write_state(ctrl, TEST_END);
	}
	if (ret < 0) {
		goto out;
	}
	end = k_uptime_get();

	ret = read_state(ctrl, EXCHANGE_RESULTS, 10000);
	if (ret < 0) {
		goto out;
	}
	snprintf(json, sizeof(json),
		 "{\"cpu_util_total\":0,\"cpu_util_user\":0,\"cpu_util_system\":0,"
		 "\"sender_has_retransmits\":-1,\"streams\":[{\"id\":1,\"bytes\":%llu,"
		 "\"retransmits\":-1,\"jitter\":0,\"errors\":0,\"packets\":0,"
		 "\"start_time\":0,\"end_time\":%d}]}", (unsigned long long)total,
		 (int)((end - t0) / 1000));
	ret = write_json(ctrl, json);
	if (ret == 0) {
		ret = skip_json(ctrl);
	}
	if (ret == 0) {
		ret = read_state(ctrl, DISPLAY_RESULTS, 10000);
	}
	if (ret == 0) {
		write_state(ctrl, IPERF_DONE);
		*bytes = total;
		printk("iperf3: %s %llu bytes in %d ms, %u kbit/s\n", reverse ? "download" : "upload",
		       (unsigned long long)total, (int)(end - t0),
		       (unsigned int)(total * 8 / (uint64_t)(end - t0)));
	}

out:
	if (ret < 0) {
		printk("iperf3: failed: %d\n", ret);
	}
	if (data >= 0) {
		zsock_close(data);
	}
	zsock_close(ctrl);
	return ret;
}

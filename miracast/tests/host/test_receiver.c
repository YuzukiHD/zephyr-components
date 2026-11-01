/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <miracast/miracast.h>

static size_t video;
static int media(void *ctx, enum miracast_codec codec, const void *data, size_t len,
		 int64_t pts, bool discontinuity)
{
	(void)ctx;
	(void)data;
	(void)pts;
	(void)discontinuity;
	if (codec == MIRACAST_H264) video += len;
	return 0;
}

static unsigned descriptors(void)
{
	DIR *dir = opendir("/proc/self/fd");
	assert(dir);
	unsigned count = 0;
	while (readdir(dir)) count++;
	closedir(dir);
	return count;
}

int main(int argc, char **argv)
{
	assert(argc == 3);
	struct miracast_receiver_config cfg = {
		.peer = "127.0.0.1", .rtsp_port = atoi(argv[1]),
		.stream = {.rtp_port = atoi(argv[2]), .aac = true}, .ops = {.media = media},
	};
	unsigned initial = descriptors();
	assert(!miracast_receiver_start(&cfg));
	assert(miracast_receiver_start(&cfg) < 0);
	int64_t until = k_uptime_get() + 10000;
	while (miracast_receiver_running() && k_uptime_get() < until) k_msleep(10);
	assert(!miracast_receiver_running());
	miracast_receiver_stop();
	assert(video > 10000);
	/* Each stop must release TCP, both UDP ports, and all media allocations. */
	for (int i = 0; i < 20; i++) {
		assert(!miracast_receiver_start(&cfg));
		k_msleep(i % 3);
		miracast_receiver_stop();
		assert(!miracast_receiver_running());
		assert(descriptors() == initial);
	}
	/* Failure after binding RTP must release it when the adjacent RTCP port is busy. */
	int busy = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(cfg.stream.rtp_port + 1)};
	assert(busy >= 0 && !bind(busy, (struct sockaddr *)&address, sizeof(address)));
	assert(!miracast_receiver_start(&cfg));
	until = k_uptime_get() + 2000;
	while (miracast_receiver_running() && k_uptime_get() < until) k_msleep(10);
	assert(!miracast_receiver_running());
	assert(descriptors() == initial + 1);
	close(busy);
	assert(!miracast_receiver_start(&cfg));
	miracast_receiver_stop();
	assert(descriptors() == initial);
	cfg.peer = "224.0.0.1";
	assert(miracast_receiver_start(&cfg) < 0 && !miracast_receiver_running());
	puts("Miracast transport and repeated start/stop: PASS");
	return 0;
}

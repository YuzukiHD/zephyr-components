/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/sys/printk.h>

#include <ios_zephyr/airplay.h>
#include "raop.h"
#include "logger.h"

static const struct airplay_config *app;
static raop_t *raop;
static dnssd_t *dnssd;


static void audio_process(void *cls, raop_ntp_t *ntp, aac_decode_struct *data)
{
	/* the picture only */
}

static void video_process(void *cls, raop_ntp_t *ntp, h264_decode_struct *data)
{
	if (app->video != NULL) {
		app->video(data->data, data->data_len, data->pts, data->frame_type == 0, app->arg);
	}
}

static void conn_init(void *cls)
{
	if (app->connection != NULL) {
		app->connection(true, app->arg);
	}
}

static void conn_destroy(void *cls)
{
	if (app->connection != NULL) {
		app->connection(false, app->arg);
	}
}

static void video_flush(void *cls)
{
	if (app->flush != NULL) {
		app->flush(app->arg);
	}
}

static void log_callback(void *cls, int level, const char *msg)
{
	if (level <= LOGGER_INFO) {
		printk("airplay: %s\n", msg);
	}
}

int airplay_start(const struct airplay_config *cfg)
{
	raop_callbacks_t cbs;
	unsigned short port = cfg->port;
	int error;

	if (raop != NULL) {
		return -EALREADY;
	}
	app = cfg;
	memset(&cbs, 0, sizeof(cbs));
	cbs.audio_process = audio_process;
	cbs.video_process = video_process;
	cbs.conn_init = conn_init;
	cbs.conn_destroy = conn_destroy;
	cbs.video_flush = video_flush;

	raop = raop_init(2, &cbs);
	if (raop == NULL) {
		return -ENOMEM;
	}
	raop_set_log_callback(raop, log_callback, NULL);
	raop_set_log_level(raop, RAOP_LOG_INFO);

	if (raop_start(raop, &port) < 0) {
		raop_destroy(raop);
		raop = NULL;
		return -EIO;
	}
	raop_set_port(raop, port);

	dnssd = dnssd_init(cfg->name, strlen(cfg->name), (const char *)cfg->hw_addr,
			   sizeof(cfg->hw_addr), &error);
	if (dnssd == NULL) {
		raop_stop(raop);
		raop_destroy(raop);
		raop = NULL;
		return -ENOMEM;
	}
	raop_set_dnssd(raop, dnssd);
	dnssd_register_raop(dnssd, port);
	dnssd_register_airplay(dnssd, port);
	printk("airplay: \"%s\" on port %u\n", cfg->name, port);

	return 0;
}

void airplay_stop(void)
{
	if (raop == NULL) {
		return;
	}
	raop_destroy(raop);
	dnssd_unregister_raop(dnssd);
	dnssd_unregister_airplay(dnssd);
	dnssd_destroy(dnssd);
	raop = NULL;
	dnssd = NULL;
}

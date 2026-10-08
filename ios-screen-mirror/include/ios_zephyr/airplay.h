/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * AirPlay screen mirroring receiver: announces itself on the network, and hands the H.264
 * stream of a mirroring iPhone to the application.
 */

#ifndef IOS_ZEPHYR_AIRPLAY_H_
#define IOS_ZEPHYR_AIRPLAY_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct airplay_config {
	/** Name in the Screen Mirroring list */
	const char *name;
	/** Address that identifies the receiver (the MAC of the network interface) */
	uint8_t hw_addr[6];
	/** TCP port, 0 for any free one */
	uint16_t port;
	/**
	 * One piece of the H.264 stream, Annex B; config is set for the SPS/PPS (a new one means
	 * the picture size may have changed). Called from the connection thread: do not block.
	 */
	void (*video)(const uint8_t *data, size_t size, uint64_t pts_us, bool config, void *arg);
	/** An iPhone connected (true) or went away (false) */
	void (*connection)(bool up, void *arg);
	/** The phone stopped mirroring: the picture should go */
	void (*flush)(void *arg);
	void *arg;
};

/** Starts the server and registers the services; 0 or a negative errno */
int airplay_start(const struct airplay_config *cfg);

void airplay_stop(void);

#endif

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The network of the player: the state of the Wi-Fi link and the time from the network. This
 * board has no Wi-Fi module to talk to, so the state is a fixed text; the interface is what the
 * settings screen uses.
 */

#include "mp5.h"

const char *net_status(void)
{
	return "No Wi-Fi module on this board";
}

const char *net_sync_status(void)
{
	return "Not available";
}

void net_sync_time(void)
{
}

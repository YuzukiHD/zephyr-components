/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_FEATURES_NET_BT_H_
#define NEKOBOX_FEATURES_NET_BT_H_

#include <stddef.h>

/**
 * Starts Bluetooth, advertises for a moment and stops it again, to see that the controller of
 * the chip answers. The chip must be running (aic8800_start); it is not stopped here.
 */
void bt_probe(void);

/** One line about Bluetooth: what the check found */
void bt_status(char *buf, size_t size);

#endif /* NEKOBOX_FEATURES_NET_BT_H_ */

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_FEATURES_NET_BT_H_
#define NEKOBOX_FEATURES_NET_BT_H_

#include <stdbool.h>
#include <stddef.h>

/** One line about Bluetooth: ready, or the device that is connected */
void bt_status(char *buf, size_t size);
/** True when a device is connected over Bluetooth */
bool bt_connected(void);

#endif /* NEKOBOX_FEATURES_NET_BT_H_ */

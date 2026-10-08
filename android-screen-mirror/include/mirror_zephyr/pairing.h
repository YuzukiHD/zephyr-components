/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* Pairing with the wireless debugging of an Android phone (the "pair with QR code" service) */

#ifndef MIRROR_ZEPHYR_PAIRING_H_
#define MIRROR_ZEPHYR_PAIRING_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pair this client with the phone
 *
 * Connects to the pairing service of the phone, proves knowledge of the password of the QR code
 * and gives the phone the public ADB key. Afterwards the phone accepts this key on its TLS
 * connect service.
 *
 * @param guid receives the name of the phone's connect service
 * @return 0 or a negative errno
 */
int adb_pair(const char *host, uint16_t port, const char *password, const char *key_path,
	     char *guid, size_t guid_size);

#ifdef __cplusplus
}
#endif

#endif

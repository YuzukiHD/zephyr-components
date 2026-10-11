/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The SD card: mounted at start, never formatted by the player */

#ifndef MP5_CORE_STORAGE_H_
#define MP5_CORE_STORAGE_H_

#include <stdbool.h>
#include <stdint.h>

#define MP5_MOUNT_POINT "/SD:"

bool storage_ready(void);
/** Total and free bytes of the SD card; false when it is not mounted */
bool storage_space(uint64_t *total, uint64_t *free_bytes);

#endif /* MP5_CORE_STORAGE_H_ */

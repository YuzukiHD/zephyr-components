/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The SD card: mounted at start, never formatted by the player. The slot has no card detect
 * signal, so a thread tries to mount a missing card every few seconds and notices a card that
 * does not answer any more.
 */

#ifndef NEKOBOX_CORE_STORAGE_H_
#define NEKOBOX_CORE_STORAGE_H_

#include <stdbool.h>
#include <stdint.h>

#define NEKOBOX_MOUNT_POINT "/SD:"

bool storage_ready(void);
/** Counts the mounts and unmounts; a screen that shows the card compares it with what it saw */
unsigned int storage_generation(void);
/** Total and free bytes of the SD card; false when it is not mounted */
bool storage_space(uint64_t *total, uint64_t *free_bytes);

#endif /* NEKOBOX_CORE_STORAGE_H_ */

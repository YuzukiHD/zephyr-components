/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MP5_HISTORY_H_
#define MP5_HISTORY_H_

#include <stdint.h>

#define HISTORY_MAX	20
#define HISTORY_PATH_MAX 192

struct history_entry {
	char path[HISTORY_PATH_MAX];
	uint32_t pos_ms;	/**< where the video was left */
	uint32_t dur_ms;
};

/** Reads the list from the SD card; an empty list when there is none */
void history_load(void);

int history_count(void);

/** The entries, the one played last first */
const struct history_entry *history_get(int index);

/** The entry of a path, or NULL */
const struct history_entry *history_find(const char *path);

/** Puts a video at the top of the list with the position it was left at, and saves the list */
void history_update(const char *path, uint32_t pos_ms, uint32_t dur_ms);

#endif /* MP5_HISTORY_H_ */

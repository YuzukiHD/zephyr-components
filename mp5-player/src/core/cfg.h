/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* The settings kept in a file on the card */

#ifndef MP5_CORE_CFG_H_
#define MP5_CORE_CFG_H_

/** Reads the settings file of the card (call after the card is mounted) */
void cfg_load(void);
/** The value of a key, NULL when it is not set */
const char *cfg_get(const char *key);
/** Sets a key (NULL deletes it) and writes the file */
void cfg_set(const char *key, const char *value);

#endif /* MP5_CORE_CFG_H_ */

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_FEATURES_DLNA_CAST_H_
#define NEKOBOX_FEATURES_DLNA_CAST_H_

#include <stdbool.h>
#include <stddef.h>

/** Starts or stops the renderer (the setting is kept in the settings file) */
void cast_enable(bool on);
bool cast_enabled(void);
/** UI-thread query: all cancelled downloads have returned and released their sockets. */
bool cast_idle(void);

/** One line for the settings: off, waiting for the network, the name, what is cast */
void cast_status(char *buf, size_t size);

#endif /* NEKOBOX_FEATURES_DLNA_CAST_H_ */

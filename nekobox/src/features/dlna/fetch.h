/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_FEATURES_DLNA_FETCH_H_
#define NEKOBOX_FEATURES_DLNA_FETCH_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/sys/atomic.h>

typedef void (*dlna_fetch_progress_fn)(void *ctx, uint64_t available, uint64_t total);

/**
 * Downloads an http:// address into the file @p path (replaced), following up to 3 redirects and
 * reading chunked bodies. @p cancel is checked every half second. Progress publishes synced bytes
 * (total is 0 when unknown). Partial files stay until the caller has closed all readers.
 * Returns 0, -ECANCELED, -ENOTSUP for an address that is not
 * plain http, or another negative errno (-EBADMSG for an HTTP error status).
 */
int dlna_fetch(const char *url, const char *path, const atomic_t *cancel,
	       dlna_fetch_progress_fn progress, void *ctx);

#endif /* NEKOBOX_FEATURES_DLNA_FETCH_H_ */

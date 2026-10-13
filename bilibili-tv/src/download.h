/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_DOWNLOAD_H_
#define BILI_DOWNLOAD_H_

#include <stdint.h>

/** Called as the file grows; a non zero return aborts the download */
typedef int (*dl_progress_fn)(uint64_t done, uint64_t total, void *ctx);

/**
 * Downloads @p url into the file @p path (replaced), continuing with a Range request after a
 * broken connection. @p total is the expected size, 0 if unknown. Returns 0 when the file is
 * complete, a negative errno otherwise (-ECANCELED after an abort; the partial file is removed).
 */
int dl_file(const char *url, const char *path, uint64_t total, dl_progress_fn fn, void *ctx);

/**
 * The same download in a thread of its own, for a file that is played while it arrives. The file exists
 * and is synced after every 64 KiB; dl_avail() (a player_growing callback) tells how much of it a
 * reader may use: *state is 0 while it runs, 1 when it is complete, negative on failure or after
 * dl_stop(). The partial file of a failed or stopped download stays for the caller to remove (a reader may still have it open). One at a time.
 */
int dl_start(const char *url, const char *path, uint64_t total, dl_progress_fn fn, void *ctx);
uint64_t dl_avail(void *ctx, int *state);
/** HTTP status of the last answer of a download (0 after a connection error), for messages */
int dl_last_status(void);
/** Stops the download and waits for its thread */
void dl_stop(void);

#endif /* BILI_DOWNLOAD_H_ */

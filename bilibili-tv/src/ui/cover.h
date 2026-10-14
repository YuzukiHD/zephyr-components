/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The cover pictures of the lists. A thread fetches the small JPEG of the image server, decodes it
 * with the video engine and keeps the XRGB8888 result in a cache. The user interface asks for a
 * cover by its address, uses the pixels while it holds a reference and gives it back; only covers
 * without a reference are thrown out when the cache is full.
 */

#ifndef BILI_COVER_H_
#define BILI_COVER_H_

#include <stdbool.h>
#include <stdint.h>

#define COVER_W	236
#define COVER_H	133

/*
 * The pixels of a cover are XRGB8888 with this many bytes from one row to the next, and the memory is
 * aligned to 64 bytes: the image drawing of LVGL needs both (a row of 472 bytes gave noise).
 */
#define COVER_STRIDE	960
#define COVER_BYTES	(COVER_STRIDE * COVER_H)

void cover_init(void);

/** Queues a fetch of the cover with the address @p pic (as in struct bili_video) unless it is known */
void cover_request(const char *pic);

/**
 * The pixels (COVER_H rows of COVER_STRIDE bytes, XRGB8888) when the cover is ready, with a reference taken, else NULL.
 * Every non NULL result has to be given back with cover_release().
 */
const uint32_t *cover_acquire(const char *pic);
void cover_release(const char *pic);

/** Increases whenever a cover became ready or failed: the user interface looks again */
uint32_t cover_version(void);

/** No fetches while the video engine belongs to the player */
void cover_pause(bool pause);

/** How many covers the user interface still holds */
int cover_in_use(void);

/** Throws out every cover without a reference and forgets the queued requests */
void cover_flush(void);

/**
 * Fetches a picture (a face) as a @p size square, decoded in software to RGB565 with the row distance
 * @p stride_bytes (a multiple of 64). Blocks; for one thread at a time. A new buffer, or NULL.
 */
uint16_t *cover_load_square(const char *pic, int size, int stride_bytes);

/** The pixels of the n-th ready cover, no reference taken: for tests only */
const uint32_t *cover_peek(int n);

/** Covers that are asked for and not there yet */
int cover_pending(void);
void cover_stats(char *buf, unsigned int size);

#endif /* BILI_COVER_H_ */

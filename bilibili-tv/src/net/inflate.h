/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A streaming decoder of raw deflate data (RFC 1951, no zlib or gzip header): the compressed bytes
 * come in pieces, the decoded bytes go to a callback. The memory is one block of about 40 KiB (the
 * window of 32 KiB and the code tables), independent of the size of the data.
 */

#ifndef BILI_INFLATE_H_
#define BILI_INFLATE_H_

#include <stddef.h>
#include <stdint.h>

/** Receives decoded bytes; a non zero return stops the decoder with that value */
typedef int (*inflate_out_fn)(void *ctx, const uint8_t *data, size_t len);

struct inflate;

struct inflate *inflate_new(inflate_out_fn out, void *ctx);
void inflate_free(struct inflate *s);

/**
 * Takes the next piece of compressed data. Returns 0 when it wants more, 1 when the last block has
 * been decoded (bytes after it are ignored), a negative errno for damaged data (-EBADMSG) or what
 * the callback returned.
 */
int inflate_feed(struct inflate *s, const uint8_t *data, size_t len);

/** After the last piece: 1 when the stream was complete, -EBADMSG when it ended early */
int inflate_end(struct inflate *s);

#endif /* BILI_INFLATE_H_ */

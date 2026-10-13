/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The receiving side of an HTTP/1.1 client without any I/O: the bytes of the connection go in,
 * the status, the interesting header fields and the decoded body come out. Content-Length,
 * chunked and read-until-close bodies are handled.
 */

#ifndef BILI_HTTP_PARSE_H_
#define BILI_HTTP_PARSE_H_

#include <stddef.h>
#include <stdint.h>

#define HTTP_HEADER_MAX	4096

/** Receives the body; a non zero return stops the parser with that value */
typedef int (*http_body_fn)(void *ctx, const uint8_t *data, size_t len);

enum http_parse_state {
	HP_HEADER,
	HP_BODY_LENGTH,
	HP_BODY_CLOSE,
	HP_CHUNK_SIZE,
	HP_CHUNK_EXT,
	HP_CHUNK_DATA,
	HP_CHUNK_CR,
	HP_TRAILER,
	HP_DONE,
};

struct http_parser {
	enum http_parse_state state;
	int status;
	int64_t content_length;		/**< -1 when not given */
	int64_t range_total;		/**< total size from Content-Range, -1 when not given */
	int chunked;
	uint64_t remaining;		/**< bytes left of the length / chunk */
	uint32_t line;			/**< length of the trailer line in progress */
	uint16_t hdr_len;
	char hdr[HTTP_HEADER_MAX];
};

void http_parser_init(struct http_parser *p);

/**
 * Feeds received bytes. Returns 0 to ask for more, 1 when the body is complete (extra bytes after
 * it are ignored), a negative errno for malformed input or the value the callback returned.
 */
int http_parser_feed(struct http_parser *p, const uint8_t *data, size_t len, http_body_fn fn,
		     void *ctx);

/** The connection was closed: 1 when that completes the response, -ECONNRESET when not */
int http_parser_eof(struct http_parser *p);

#endif /* BILI_HTTP_PARSE_H_ */

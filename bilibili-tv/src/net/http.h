/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* A blocking HTTP/1.1 GET over Zephyr sockets, with TLS for https URLs */

#ifndef BILI_HTTP_H_
#define BILI_HTTP_H_

#include <stddef.h>
#include <stdint.h>

#include "net/http_parse.h"

struct http_opts {
	const char *headers;	/**< extra header lines, each ending in CRLF, may be NULL */
	int64_t range_start;	/**< >= 0 asks for the bytes from this offset on */
	int timeout_ms;		/**< no data for this long ends the request, 0 is 15 s */
	int tls_verify;
};

struct http_result {
	int status;
	int64_t content_length;
	int64_t range_total;
	uint32_t connect_ms;	/**< until the connection (and the TLS handshake) is up */
	uint32_t first_byte_ms;	/**< until the first byte of the answer, from the start */
	uint32_t total_ms;
	char set_cookie[640];	/**< "name=value; name=value" of the Set-Cookie fields of the answer */
};

/**
 * Sends every request through an HTTP proxy ("host:port", NULL or "" for none): http:// URLs as
 * absolute-address requests, https:// URLs through a CONNECT tunnel with the TLS handshake done
 * here (the server certificate is not checked in that case).
 */
void http_set_proxy(const char *hostport);

/**
 * Fetches @p url and hands the body to @p fn (only for 2xx answers; the body of anything else is
 * read and dropped). Returns 0 when the answer is complete, whatever its status, else a negative
 * errno, or what @p fn returned.
 */
int http_get(const char *url, const struct http_opts *opts, http_body_fn fn, void *ctx,
	     struct http_result *res);

/** Fetches a small answer into @p buf (NUL terminated); returns its length or a negative errno */
int http_get_text(const char *url, const struct http_opts *opts, char *buf, size_t size,
		  struct http_result *res);

#endif /* BILI_HTTP_H_ */

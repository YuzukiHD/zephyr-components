/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "http_parse.h"

void http_parser_init(struct http_parser *p)
{
	memset(p, 0, sizeof(*p));
	p->state = HP_HEADER;
	p->content_length = -1;
	p->range_total = -1;
}

/* the value of a header field of the block in p->hdr, NULL when it is not there */
static const char *field(struct http_parser *p, const char *name)
{
	size_t n = strlen(name);
	const char *l = strstr(p->hdr, "\r\n");

	while (l != NULL) {
		l += 2;
		if (strncasecmp(l, name, n) == 0 && l[n] == ':') {
			l += n + 1;
			while (*l == ' ') {
				l++;
			}
			return l;
		}
		l = strstr(l, "\r\n");
	}

	return NULL;
}

static int headers_done(struct http_parser *p)
{
	const char *v;

	if (strncmp(p->hdr, "HTTP/1.", 7) != 0 || p->hdr[8] != ' ') {
		return -EBADMSG;
	}
	p->status = atoi(p->hdr + 9);
	v = field(p, "Transfer-Encoding");
	p->chunked = v != NULL && strncasecmp(v, "chunked", 7) == 0;
	v = field(p, "Content-Length");
	if (v != NULL) {
		p->content_length = strtoll(v, NULL, 10);
	}
	v = field(p, "Content-Range");
	if (v != NULL && strncasecmp(v, "bytes", 5) == 0) {
		const char *slash = strchr(v, '/');

		if (slash != NULL && slash[1] != '*') {
			p->range_total = strtoll(slash + 1, NULL, 10);
		}
	}
	/* no body */
	if (p->status == 204 || p->status == 304 || (p->status >= 100 && p->status < 200)) {
		p->state = HP_DONE;
	} else if (p->chunked) {
		p->state = HP_CHUNK_SIZE;
		p->remaining = 0;
		p->line = 0;
	} else if (p->content_length >= 0) {
		p->remaining = p->content_length;
		p->state = p->remaining == 0 ? HP_DONE : HP_BODY_LENGTH;
	} else {
		p->state = HP_BODY_CLOSE;
	}

	return 0;
}

int http_parser_feed(struct http_parser *p, const uint8_t *d, size_t n, http_body_fn fn, void *ctx)
{
	while (n > 0 && p->state != HP_DONE) {
		int ret;

		switch (p->state) {
		case HP_HEADER:
			if ((size_t)p->hdr_len + 1 >= sizeof(p->hdr)) {
				return -EMSGSIZE;
			}
			p->hdr[p->hdr_len++] = *d++;
			n--;
			p->hdr[p->hdr_len] = '\0';
			if (p->hdr_len >= 4 && memcmp(p->hdr + p->hdr_len - 4, "\r\n\r\n", 4) == 0) {
				ret = headers_done(p);
				if (ret < 0) {
					return ret;
				}
			}
			break;

		case HP_BODY_LENGTH:
		case HP_CHUNK_DATA: {
			size_t take = n < p->remaining ? n : (size_t)p->remaining;

			ret = fn(ctx, d, take);
			if (ret != 0) {
				return ret;
			}
			d += take;
			n -= take;
			p->remaining -= take;
			if (p->remaining == 0) {
				p->state = p->state == HP_BODY_LENGTH ? HP_DONE : HP_CHUNK_CR;
				p->line = 0;
			}
			break;
		}

		case HP_BODY_CLOSE:
			ret = fn(ctx, d, n);
			if (ret != 0) {
				return ret;
			}
			return 0;

		case HP_CHUNK_SIZE: {
			int c = *d++;

			n--;
			if (c == '\r') {
				break;
			}
			if (c == '\n') {
				if (p->line == 0) {
					return -EBADMSG;
				}
				if (p->remaining == 0) {
					p->state = HP_TRAILER;
					p->line = 0;
				} else {
					p->state = HP_CHUNK_DATA;
				}
			} else if (isxdigit(c)) {
				if (p->remaining >> 56) {
					return -EBADMSG;
				}
				p->remaining = (p->remaining << 4) |
					       (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
				p->line++;
			} else if (c == ';' && p->line > 0) {
				p->state = HP_CHUNK_EXT;
			} else {
				return -EBADMSG;
			}
			break;
		}

		case HP_CHUNK_EXT:
			if (*d++ == '\n') {
				p->state = p->remaining == 0 ? HP_TRAILER : HP_CHUNK_DATA;
				p->line = 0;
			}
			n--;
			break;

		case HP_CHUNK_CR: {
			/* the CRLF behind the data of a chunk */
			int c = *d++;

			n--;
			if (c == '\n') {
				p->state = HP_CHUNK_SIZE;
				p->line = 0;
				p->remaining = 0;
			} else if (c != '\r') {
				return -EBADMSG;
			}
			break;
		}

		case HP_TRAILER: {
			/* trailer fields are skipped; an empty line ends the message */
			int c = *d++;

			n--;
			if (c == '\n') {
				if (p->line == 0) {
					p->state = HP_DONE;
				}
				p->line = 0;
			} else if (c != '\r') {
				p->line++;
			}
			break;
		}

		default:
			return -EINVAL;
		}
	}

	return p->state == HP_DONE ? 1 : 0;
}

int http_parser_eof(struct http_parser *p)
{
	return p->state == HP_BODY_CLOSE || p->state == HP_DONE ? 1 : -ECONNRESET;
}

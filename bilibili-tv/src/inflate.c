/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "inflate.h"

#define WINDOW		32768
#define OUT_CHUNK	512
#define MAX_BITS	15
#define MAX_LIT		288
#define MAX_DIST	30

/* a canonical Huffman code: how many codes of each length, and the symbols in code order */
struct huff {
	uint16_t count[MAX_BITS + 1];
	uint16_t symbol[MAX_LIT];
};

enum state {
	S_BLOCK,	/* the 3 bit header of a block */
	S_STORED_LEN,
	S_STORED,
	S_DYN_COUNTS,
	S_DYN_CLEN,	/* the lengths of the code length code */
	S_DYN_LENS,	/* the lengths of the literal and the distance codes */
	S_CODES,
	S_DONE,
	S_ERROR,
};

struct inflate {
	inflate_out_fn out;
	void *ctx;
	enum state state;
	uint8_t last;			/* the block in work is the last */
	const uint8_t *in;
	size_t in_len;
	uint64_t bits;
	int nbits;
	uint32_t wpos;			/* total number of bytes written to the window */
	uint16_t stored_left;
	uint16_t nlen, ndist, ncode, idx;
	uint8_t lengths[MAX_LIT + MAX_DIST];
	struct huff lit, dist, clen;
	uint16_t obuf_n;
	uint8_t obuf[OUT_CHUNK];
	uint8_t win[WINDOW];
};

static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
				      67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
				      5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
				       769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10,
				       11, 11, 12, 12, 13, 13};
static const uint8_t clen_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct inflate *inflate_new(inflate_out_fn out, void *ctx)
{
	struct inflate *s = calloc(1, sizeof(*s));

	if (s != NULL) {
		s->out = out;
		s->ctx = ctx;
		s->state = S_BLOCK;
	}

	return s;
}

void inflate_free(struct inflate *s)
{
	free(s);
}

/* builds a code from the lengths of its symbols; <0 for an over-subscribed code, >0 for an incomplete one */
static int huff_build(struct huff *h, const uint8_t *length, int n)
{
	uint16_t offs[MAX_BITS + 1];
	int left = 1;

	memset(h->count, 0, sizeof(h->count));
	for (int i = 0; i < n; i++) {
		h->count[length[i]]++;
	}
	if (h->count[0] == n) {
		return 0;	/* no codes at all: allowed for the distance code */
	}
	for (int len = 1; len <= MAX_BITS; len++) {
		left = (left << 1) - h->count[len];
		if (left < 0) {
			return left;
		}
	}
	offs[1] = 0;
	for (int len = 1; len < MAX_BITS; len++) {
		offs[len + 1] = offs[len] + h->count[len];
	}
	for (int i = 0; i < n; i++) {
		if (length[i] != 0) {
			h->symbol[offs[length[i]]++] = i;
		}
	}

	return left;
}

/* the bits of the input as far as they are there; the input is refilled from the bytes of the piece */
static void refill(struct inflate *s)
{
	while (s->nbits <= 56 && s->in_len > 0) {
		s->bits |= (uint64_t)*s->in++ << s->nbits;
		s->nbits += 8;
		s->in_len--;
	}
}

/* reads n bits (n <= 16) when they are there; -1 when more input is needed */
static int take(struct inflate *s, int n)
{
	int v;

	refill(s);
	if (s->nbits < n) {
		return -1;
	}
	v = s->bits & ((1u << n) - 1);
	s->bits >>= n;
	s->nbits -= n;

	return v;
}

/*
 * Looks at the code that starts @p skip bits into the input without taking anything: the symbol and
 * the number of bits it uses. Returns -1 when the bits so far are not enough to know (more input is
 * needed), -2 when there is no such code.
 */
static int peek_symbol(struct inflate *s, const struct huff *h, int skip, int *used)
{
	int code = 0, first = 0, index = 0;

	refill(s);
	for (int len = 1; len <= MAX_BITS; len++) {
		int count;

		if (skip + len > s->nbits) {
			return -1;
		}
		code |= (s->bits >> (skip + len - 1)) & 1;
		count = h->count[len];
		if (code - count < first) {
			*used = len;
			return h->symbol[index + (code - first)];
		}
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}

	return -2;
}

static int emit(struct inflate *s, int flush)
{
	int ret = 0;

	if (s->obuf_n > 0 && (flush || s->obuf_n == OUT_CHUNK)) {
		ret = s->out(s->ctx, s->obuf, s->obuf_n);
		s->obuf_n = 0;
	}

	return ret;
}

static int put(struct inflate *s, uint8_t b)
{
	s->win[s->wpos++ & (WINDOW - 1)] = b;
	s->obuf[s->obuf_n++] = b;

	return s->obuf_n == OUT_CHUNK ? emit(s, 0) : 0;
}

/* one step of the decoder; 0 goes on, 1 means "needs more input", 2 the stream is complete, <0 an error */
static int step(struct inflate *s)
{
	int v, used, sym;

	switch (s->state) {
	case S_BLOCK:
		v = take(s, 3);
		if (v < 0) {
			return 1;
		}
		s->last = v & 1;
		switch (v >> 1) {
		case 0:
			/* the rest of the byte is dropped */
			s->bits >>= s->nbits & 7;
			s->nbits -= s->nbits & 7;
			s->state = S_STORED_LEN;
			break;
		case 1: {
			uint8_t l[MAX_LIT + MAX_DIST];

			for (int i = 0; i < 144; i++) {
				l[i] = 8;
			}
			for (int i = 144; i < 256; i++) {
				l[i] = 9;
			}
			for (int i = 256; i < 280; i++) {
				l[i] = 7;
			}
			for (int i = 280; i < MAX_LIT; i++) {
				l[i] = 8;
			}
			huff_build(&s->lit, l, MAX_LIT);
			for (int i = 0; i < MAX_DIST; i++) {
				l[i] = 5;
			}
			huff_build(&s->dist, l, MAX_DIST);
			s->state = S_CODES;
			break;
		}
		case 2:
			s->state = S_DYN_COUNTS;
			break;
		default:
			return -EBADMSG;
		}
		return 0;

	case S_STORED_LEN:
		refill(s);
		if (s->nbits < 32) {
			return 1;
		}
		v = s->bits & 0xffff;
		if ((int)((s->bits >> 16) & 0xffff) != (~v & 0xffff)) {
			return -EBADMSG;
		}
		s->bits >>= 32;
		s->nbits -= 32;
		s->stored_left = v;
		s->state = S_STORED;
		return 0;

	case S_STORED:
		while (s->stored_left > 0) {
			v = take(s, 8);
			if (v < 0) {
				return 1;
			}
			if (put(s, v) != 0) {
				return -ECANCELED;
			}
			s->stored_left--;
		}
		s->state = s->last ? S_DONE : S_BLOCK;
		return 0;

	case S_DYN_COUNTS:
		refill(s);
		if (s->nbits < 14) {
			return 1;
		}
		s->nlen = (s->bits & 31) + 257;
		s->ndist = ((s->bits >> 5) & 31) + 1;
		s->ncode = ((s->bits >> 10) & 15) + 4;
		s->bits >>= 14;
		s->nbits -= 14;
		if (s->nlen > 286 || s->ndist > 30) {
			return -EBADMSG;
		}
		memset(s->lengths, 0, 19);
		s->idx = 0;
		s->state = S_DYN_CLEN;
		return 0;

	case S_DYN_CLEN:
		while (s->idx < s->ncode) {
			v = take(s, 3);
			if (v < 0) {
				return 1;
			}
			s->lengths[clen_order[s->idx++]] = v;
		}
		if (huff_build(&s->clen, s->lengths, 19) != 0) {
			return -EBADMSG;
		}
		memset(s->lengths, 0, sizeof(s->lengths));
		s->idx = 0;
		s->state = S_DYN_LENS;
		return 0;

	case S_DYN_LENS:
		while (s->idx < s->nlen + s->ndist) {
			int extra = 0, base = 0, value = 0;

			sym = peek_symbol(s, &s->clen, 0, &used);
			if (sym == -1) {
				return 1;
			}
			if (sym < 0) {
				return -EBADMSG;
			}
			if (sym < 16) {
				value = sym;
				base = 1;
			} else if (sym == 16) {
				if (s->idx == 0) {
					return -EBADMSG;
				}
				value = s->lengths[s->idx - 1];
				extra = 2;
				base = 3;
			} else if (sym == 17) {
				extra = 3;
				base = 3;
			} else {
				extra = 7;
				base = 11;
			}
			if (s->nbits < used + extra) {
				return 1;
			}
			s->bits >>= used;
			s->nbits -= used;
			if (extra > 0) {
				base += s->bits & ((1u << extra) - 1);
				s->bits >>= extra;
				s->nbits -= extra;
			}
			if (s->idx + base > s->nlen + s->ndist) {
				return -EBADMSG;
			}
			while (base-- > 0) {
				s->lengths[s->idx++] = value;
			}
		}
		if (s->lengths[256] == 0) {
			return -EBADMSG;	/* no end-of-block code */
		}
		if (huff_build(&s->lit, s->lengths, s->nlen) < 0 ||
		    huff_build(&s->dist, s->lengths + s->nlen, s->ndist) < 0) {
			return -EBADMSG;
		}
		s->state = S_CODES;
		return 0;

	case S_CODES:
		for (;;) {
			int lused, dsym, dused, len, dist, total;

			sym = peek_symbol(s, &s->lit, 0, &lused);
			if (sym == -1) {
				return 1;
			}
			if (sym < 0) {
				return -EBADMSG;
			}
			if (sym < 256) {
				s->bits >>= lused;
				s->nbits -= lused;
				if (put(s, sym) != 0) {
					return -ECANCELED;
				}
				continue;
			}
			if (sym == 256) {
				s->bits >>= lused;
				s->nbits -= lused;
				s->state = s->last ? S_DONE : S_BLOCK;
				return 0;
			}
			sym -= 257;
			if (sym >= 29) {
				return -EBADMSG;
			}
			/*
			 * The length code, its extra bits, the distance code and its extra bits are looked at
			 * together and taken together: nothing is consumed unless all of them are there.
			 */
			total = lused + len_extra[sym];
			if (s->nbits < total) {
				return 1;
			}
			len = len_base[sym] + ((s->bits >> lused) & ((1u << len_extra[sym]) - 1));
			dsym = peek_symbol(s, &s->dist, total, &dused);
			if (dsym == -1) {
				return 1;
			}
			if (dsym < 0 || dsym >= 30) {
				return -EBADMSG;
			}
			total += dused;
			if (s->nbits < total + dist_extra[dsym]) {
				return 1;
			}
			dist = dist_base[dsym] + ((s->bits >> total) & ((1u << dist_extra[dsym]) - 1));
			total += dist_extra[dsym];
			s->bits >>= total;
			s->nbits -= total;
			if ((uint32_t)dist > s->wpos) {
				return -EBADMSG;
			}
			while (len-- > 0) {
				if (put(s, s->win[(s->wpos - dist) & (WINDOW - 1)]) != 0) {
					return -ECANCELED;
				}
			}
		}

	default:
		return 2;
	}
}

int inflate_feed(struct inflate *s, const uint8_t *data, size_t len)
{
	int ret;

	if (s->state == S_ERROR) {
		return -EBADMSG;
	}
	s->in = data;
	s->in_len = len;
	for (;;) {
		ret = step(s);
		if (ret == 0) {
			continue;
		}
		break;
	}
	if (ret == 1) {
		ret = emit(s, 1);
		return ret != 0 ? ret : 0;
	}
	if (ret < 0) {
		s->state = S_ERROR;
		return ret;
	}
	ret = emit(s, 1);

	return ret != 0 ? ret : 1;
}

int inflate_end(struct inflate *s)
{
	return s->state == S_DONE ? 1 : -EBADMSG;
}

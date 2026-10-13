/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "danmaku.h"

#define TAG_MAX		200
#define RAW_MAX		400	/* the text of a comment before the entities are decoded */

enum pstate {
	P_OUT,		/* between tags: the text of a <d> element when in_d is set */
	P_TAG,		/* inside <...> */
};

struct dm_parser {
	struct dm_set *set;
	enum pstate state;
	bool in_d;			/* inside a <d> element: the text is kept */
	uint16_t tag_n, raw_n;
	char tag[TAG_MAX];
	char raw[RAW_MAX];
	/* the fields of the p attribute of the element in work */
	uint32_t t_ms, rgb;
	uint8_t mode;
};

void dm_init(struct dm_set *set, uint32_t max_items, uint32_t pool_max)
{
	memset(set, 0, sizeof(*set));
	set->max_items = max_items;
	set->pool_max = pool_max;
}

void dm_free(struct dm_set *set)
{
	free(set->items);
	free(set->pool);
	memset(set, 0, sizeof(*set));
}

struct dm_parser *dm_parser_new(struct dm_set *set)
{
	struct dm_parser *p = calloc(1, sizeof(*p));

	if (p != NULL) {
		p->set = set;
	}

	return p;
}

void dm_parser_free(struct dm_parser *p)
{
	free(p);
}

/* the comma separated numbers of the attribute: time (seconds with a fraction), mode, size, color */
static int parse_p(struct dm_parser *p, const char *attr)
{
	uint32_t whole = 0, frac = 0;
	int digits = 0;

	if (*attr < '0' || *attr > '9') {
		return -1;
	}
	for (; *attr >= '0' && *attr <= '9'; attr++) {
		whole = whole * 10 + (*attr - '0');
	}
	if (*attr == '.') {
		for (attr++; *attr >= '0' && *attr <= '9'; attr++) {
			if (digits < 3) {
				frac = frac * 10 + (*attr - '0');
				digits++;
			}
		}
	}
	while (digits++ < 3) {
		frac *= 10;
	}
	p->t_ms = whole * 1000 + frac;
	for (int field = 1; field <= 3; field++) {
		uint32_t v = 0;

		if (*attr != ',') {
			return -1;
		}
		for (attr++; *attr >= '0' && *attr <= '9'; attr++) {
			v = v * 10 + (*attr - '0');
		}
		if (field == 1) {
			p->mode = v < 256 ? v : 0;
		} else if (field == 3) {
			p->rgb = v & 0xffffff;
		}
	}

	return 0;
}

/* writes the code point as UTF-8; returns the bytes */
static int utf8(uint32_t cp, char *out)
{
	if (cp < 0x80) {
		out[0] = cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = 0xc0 | (cp >> 6);
		out[1] = 0x80 | (cp & 0x3f);
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = 0xe0 | (cp >> 12);
		out[1] = 0x80 | ((cp >> 6) & 0x3f);
		out[2] = 0x80 | (cp & 0x3f);
		return 3;
	}
	out[0] = 0xf0 | (cp >> 18);
	out[1] = 0x80 | ((cp >> 12) & 0x3f);
	out[2] = 0x80 | ((cp >> 6) & 0x3f);
	out[3] = 0x80 | (cp & 0x3f);

	return 4;
}

/* entities decoded, line breaks and other control characters made into spaces, cut at DM_TEXT_MAX */
static int clean(const char *raw, int n, char *out)
{
	int o = 0;

	for (int i = 0; i < n && o < DM_TEXT_MAX - 4; i++) {
		unsigned char c = raw[i];

		if (c == '&') {
			static const struct {
				const char *name;
				char ch;
			} ents[] = {{"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'}, {"apos;", '\''}};
			bool done = false;

			for (size_t k = 0; k < sizeof(ents) / sizeof(ents[0]) && !done; k++) {
				size_t l = strlen(ents[k].name);

				if (i + 1 + (int)l <= n && strncmp(raw + i + 1, ents[k].name, l) == 0) {
					out[o++] = ents[k].ch;
					i += l;
					done = true;
				}
			}
			if (!done && i + 2 < n && raw[i + 1] == '#') {
				uint32_t cp = 0;
				int j = i + 2;

				for (; j < n && raw[j] >= '0' && raw[j] <= '9' && cp < 0x110000; j++) {
					cp = cp * 10 + (raw[j] - '0');
				}
				if (j < n && raw[j] == ';' && cp >= 32 && cp < 0x110000) {
					o += utf8(cp, out + o);
					i = j;
					done = true;
				}
			}
			if (!done) {
				out[o++] = '&';
			}
		} else if (c <= 32 || c == 127) {
			if (o > 0 && out[o - 1] != ' ') {
				out[o++] = ' ';
			}
		} else {
			out[o++] = c;
		}
	}
	/* a character that was cut: drop what is left of it */
	if (o == DM_TEXT_MAX - 4) {
		int i = o;

		while (i > 0 && ((unsigned char)out[i - 1] & 0xc0) == 0x80) {
			i--;
		}
		if (i > 0 && (unsigned char)out[i - 1] >= 0xc0) {
			unsigned char lead = out[i - 1];
			int need = lead >= 0xf0 ? 4 : (lead >= 0xe0 ? 3 : 2);

			if (o - (i - 1) < need) {
				o = i - 1;
			}
		}
	}
	while (o > 0 && out[o - 1] == ' ') {
		o--;
	}

	return o;
}

static int add_item(struct dm_parser *p)
{
	struct dm_set *s = p->set;
	char text[DM_TEXT_MAX];
	int n;
	struct dm_item *it;

	if (p->mode != DM_SCROLL && p->mode != DM_BOTTOM && p->mode != DM_TOP && p->mode != DM_REVERSE) {
		s->skipped++;
		return 0;
	}
	n = clean(p->raw, p->raw_n, text);
	if (n == 0 || s->count >= s->max_items || s->pool_used + n + 1 > s->pool_max) {
		s->skipped++;
		return 0;
	}
	if (s->count == s->cap) {
		uint32_t cap = s->cap ? s->cap * 2 : 256;
		struct dm_item *items;

		if (cap > s->max_items) {
			cap = s->max_items;
		}
		items = realloc(s->items, cap * sizeof(*items));
		if (items == NULL) {
			return -ENOMEM;
		}
		s->items = items;
		s->cap = cap;
	}
	if (s->pool_used + n + 1 > s->pool_cap) {
		uint32_t cap = s->pool_cap ? s->pool_cap * 2 : 4096;
		char *pool;

		while (cap < s->pool_used + n + 1) {
			cap *= 2;
		}
		if (cap > s->pool_max) {
			cap = s->pool_max;
		}
		pool = realloc(s->pool, cap);
		if (pool == NULL) {
			return -ENOMEM;
		}
		s->pool = pool;
		s->pool_cap = cap;
	}
	memcpy(s->pool + s->pool_used, text, n);
	s->pool[s->pool_used + n] = '\0';
	it = &s->items[s->count++];
	it->t_ms = p->t_ms;
	it->rgb = p->rgb;
	it->mode = p->mode;
	it->len = n;
	it->off = s->pool_used;
	s->pool_used += n + 1;

	return 0;
}

static int tag_done(struct dm_parser *p)
{
	p->state = P_OUT;
	p->tag[p->tag_n] = '\0';
	if (strncmp(p->tag, "d p=\"", 5) == 0) {
		p->raw_n = 0;
		p->mode = 0;
		p->in_d = true;
		if (parse_p(p, p->tag + 5) != 0) {
			p->mode = 0;
		}
	} else if (strcmp(p->tag, "/d") == 0 && p->in_d) {
		p->in_d = false;
		return add_item(p);
	}

	return 0;
}

int dm_parser_feed(struct dm_parser *p, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		char c = data[i];

		if (p->state == P_OUT) {
			if (c == '<') {
				p->tag_n = 0;
				p->state = P_TAG;
			} else if (p->in_d && p->raw_n < RAW_MAX) {
				p->raw[p->raw_n++] = c;
			}
		} else if (c == '>') {
			int ret = tag_done(p);

			if (ret != 0) {
				return ret;
			}
		} else if (p->tag_n < TAG_MAX - 1) {
			p->tag[p->tag_n++] = c;
		}
	}

	return 0;
}

static int cmp_time(const void *a, const void *b)
{
	const struct dm_item *x = a, *y = b;

	return x->t_ms < y->t_ms ? -1 : (x->t_ms > y->t_ms ? 1 : (int)x->off - (int)y->off);
}

void dm_parser_finish(struct dm_parser *p)
{
	if (p->set->count > 1) {
		qsort(p->set->items, p->set->count, sizeof(struct dm_item), cmp_time);
	}
}

uint32_t dm_find(const struct dm_set *set, uint32_t t_ms)
{
	uint32_t lo = 0, hi = set->count;

	while (lo < hi) {
		uint32_t mid = (lo + hi) / 2;

		if (set->items[mid].t_ms < t_ms) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}

	return lo;
}

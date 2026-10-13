/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The comments of a video ("danmaku"): the list the server sends is an XML text of lines
 *   <d p="time,mode,size,color,...">text</d>
 * which is read piece by piece (the text comes out of the decoder in small pieces) into a flat array
 * of items, sorted by time.
 */

#ifndef BILI_DANMAKU_H_
#define BILI_DANMAKU_H_

#include <stddef.h>
#include <stdint.h>

enum dm_mode {
	DM_SCROLL = 1,		/**< right to left */
	DM_BOTTOM = 4,
	DM_TOP = 5,
	DM_REVERSE = 6,		/**< left to right */
};

#define DM_TEXT_MAX	90	/**< bytes of a text, cut on a character */

struct dm_item {
	uint32_t t_ms;
	uint32_t rgb;		/**< 0xRRGGBB */
	uint32_t off;		/**< position of the text in the pool */
	uint8_t mode;		/**< enum dm_mode */
	uint8_t len;		/**< bytes of the text, without a terminator */
};

struct dm_set {
	struct dm_item *items;
	uint32_t count, cap, max_items;
	char *pool;
	uint32_t pool_used, pool_cap, pool_max;
	uint32_t skipped;	/**< comments left out: other kinds, empty, or over the limits */
};

void dm_init(struct dm_set *set, uint32_t max_items, uint32_t pool_max);
void dm_free(struct dm_set *set);

static inline const char *dm_text(const struct dm_set *set, const struct dm_item *it)
{
	return set->pool + it->off;
}

struct dm_parser;

struct dm_parser *dm_parser_new(struct dm_set *set);
void dm_parser_free(struct dm_parser *p);

/** The next piece of the XML text; 0, or -ENOMEM when the memory ran out (the items so far stay) */
int dm_parser_feed(struct dm_parser *p, const uint8_t *data, size_t len);

/** After the last piece: sorts the items by time */
void dm_parser_finish(struct dm_parser *p);

/** Index of the first item at or after @p t_ms (set->count when there is none) */
uint32_t dm_find(const struct dm_set *set, uint32_t t_ms);

#endif /* BILI_DANMAKU_H_ */

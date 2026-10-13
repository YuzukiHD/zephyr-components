/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_API_H_
#define BILI_API_H_

#include "bili_parse.h"
#include "danmaku.h"

/*
 * All calls block, talk to the server and are meant for one thread (the worker): the signing key
 * and the cookie they keep are not protected. They return the number of items (or 0 for success)
 * or a negative errno.
 */

/** A page of the recommendation feed (page counts from 1) */
int bili_api_feed(int page, struct bili_video *out, int max);

/** A page of the popular list (page counts from 1) */
int bili_api_popular(int page, struct bili_video *out, int max);

/** A page of the results for a keyword (UTF-8); @p pages gets the number of pages */
int bili_api_search(const char *keyword, int page, struct bili_video *out, int max, int *pages);

/** The trending search words */
int bili_api_hot_words(char words[][BILI_HOT_WORD_LEN], int max);

/** The 360P file of a video; a video without cid (a search result) is looked up first */
int bili_api_stream(const struct bili_video *v, struct bili_stream *out);

/**
 * The comments of a video (no login needed): fetched, inflated and read into @p set (which the caller
 * has set up with dm_init() and frees). Returns the number of comments, or a negative errno; a broken
 * transfer keeps what was read until then.
 */
int bili_api_danmaku(uint64_t cid, struct dm_set *set);

#endif /* BILI_API_H_ */

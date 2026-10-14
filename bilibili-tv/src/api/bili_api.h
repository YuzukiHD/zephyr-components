/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_API_H_
#define BILI_API_H_

#include <stdbool.h>
#include <stddef.h>

#include "api/bili_parse.h"
#include "api/danmaku.h"

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

/**
 * The followed users' videos (a login is needed); page 1 starts again, the next ones continue from
 * where the last answer ended. @p more tells whether the server has more.
 */
int bili_api_follow(int page, struct bili_video *out, int max, bool *more);

/** A new QR login: @p url is what the code shows, @p key is for bili_api_qr_poll() */
int bili_api_qr_new(char *url, size_t url_size, char *key, size_t key_size);

/**
 * Asks how the scan is going. @p state: 86101 not scanned, 86090 scanned and waiting for the
 * confirmation, 86038 the code is out of date, 0 logged in (the cookies are saved to the account).
 * Returns 0 or a negative errno.
 */
int bili_api_qr_poll(const char *key, int *state);

/** The name and the picture address of the logged in user, -EACCES when the login is not valid (any more) */
int bili_api_user(char *name, size_t size, char *face, size_t face_size);

/** The coins, dynamics, followed users and followers of the logged in user */
int bili_api_user_stats(struct bili_user_stats *out);

#endif /* BILI_API_H_ */

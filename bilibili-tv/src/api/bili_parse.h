/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* What the answers of the web API are made into; no I/O here */

#ifndef BILI_PARSE_H_
#define BILI_PARSE_H_

#include <stddef.h>
#include <stdint.h>

struct bili_video {
	char bvid[16];
	char title[160];
	char author[40];
	char cover[160];
	uint64_t cid;		/**< 0 for a search result: bili_parse_view_cid() asks for it */
	uint32_t duration;	/**< seconds */
	uint32_t views;
	uint32_t danmaku;
};

#define BILI_HOT_WORD_LEN	40

struct bili_stream {
	char url[512];
	uint64_t size;		/**< bytes of the file */
	uint32_t length_ms;
	int quality;		/**< 16 is 360P, 32 480P, 64 720P */
	uint64_t cid;		/**< of the video (a search result gets it looked up), set by bili_api_stream() */
};

/**
 * Checks the envelope of an answer: 0 when "code" is 0, else the (positive) code of the server
 * or -EBADMSG when there is none.
 */
int bili_parse_code(const char *json);

/** The WBI key from the nav answer (which also comes as code -101 without a login) */
int bili_parse_nav(const char *json, char key[33]);

/** The videos of the recommendation feed; entries that are no plain video (ads, live) are skipped */
int bili_parse_feed(const char *json, struct bili_video *out, int max);

/** The popular list (/x/web-interface/popular); same result as bili_parse_feed() */
int bili_parse_popular(const char *json, struct bili_video *out, int max);

/**
 * The results of a video search. The marks around the found words are removed from the titles.
 * @p pages gets the number of result pages (may be NULL). Returns the number of videos.
 */
int bili_parse_search(const char *json, struct bili_video *out, int max, int *pages);

/** The trending search words; returns how many were copied into @p words */
int bili_parse_hot_words(const char *json, char words[][BILI_HOT_WORD_LEN], int max);

/**
 * The cid of the first part of a video from /x/player/pagelist (data is an array). The answer of
 * /x/web-interface/view also works, but it can be 150 KB for a video of a large series.
 */
int bili_parse_view_cid(const char *json, uint64_t *cid);

/** Removes <tags> from @p s in place and decodes the common HTML entities */
void bili_strip_html(char *s);
/** Full width Latin letters, digits and punctuation become the ordinary ones (in place) */
void bili_fold_width(char *s);

/** "75", "2:05", "1:02:03" (fields without leading zeros are fine) to seconds, 0 when not a time */
uint32_t bili_parse_clock(const char *s);

/**
 * The address of a cover scaled by the image server to @p w x @p h (cropped), as an http URL;
 * a protocol-less or https address is turned into http. Returns the length or a negative errno.
 */
int bili_cover_url(const char *pic, int w, int h, char *out, size_t size);

/** The first file of a playurl answer */
int bili_parse_playurl(const char *json, struct bili_stream *out);

#endif /* BILI_PARSE_H_ */

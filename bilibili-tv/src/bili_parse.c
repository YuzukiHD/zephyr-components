/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bili_parse.h"
#include "json.h"
#include "wbi.h"

int bili_parse_code(const char *json)
{
	const char *c = js_get(json, "code");

	if (c == NULL) {
		return -EBADMSG;
	}

	return (int)js_int(c, -EBADMSG);
}

int bili_parse_nav(const char *json, char key[33])
{
	char img[128], sub[128];
	const char *w = js_path(json, "data.wbi_img");

	if (js_str(js_get(w, "img_url"), img, sizeof(img)) < 0 ||
	    js_str(js_get(w, "sub_url"), sub, sizeof(sub)) < 0) {
		return -EBADMSG;
	}

	return wbi_make_key(img, sub, key);
}

/* a number that the server sometimes sends as a string ("85158") */
static long long num(const char *v, long long def)
{
	if (v != NULL && *v == '"') {
		v++;
	}

	return js_int(v, def);
}

void bili_fold_width(char *s);

static void fill_common(const char *it, struct bili_video *v)
{
	bili_fold_width(v->title);
	if (js_str(js_path(it, "owner.name"), v->author, sizeof(v->author)) < 0 &&
	    js_str(js_get(it, "author"), v->author, sizeof(v->author)) < 0) {
		v->author[0] = '\0';
	}
	bili_fold_width(v->author);
	if (js_str(js_get(it, "pic"), v->cover, sizeof(v->cover)) < 0) {
		v->cover[0] = '\0';
	}
	v->cid = num(js_get(it, "cid"), 0);
	v->views = num(js_path(it, "stat.view"), num(js_get(it, "play"), 0));
	v->danmaku = num(js_path(it, "stat.danmaku"), num(js_get(it, "danmaku"), 0));
}

static int parse_video(const char *it, struct bili_video *v)
{
	if (!js_is(js_get(it, "goto"), "av") ||
	    js_str(js_get(it, "bvid"), v->bvid, sizeof(v->bvid)) <= 0 ||
	    js_str(js_get(it, "title"), v->title, sizeof(v->title)) < 0) {
		return -EBADMSG;
	}
	fill_common(it, v);
	v->duration = js_int(js_get(it, "duration"), 0);

	return v->cid != 0 ? 0 : -EBADMSG;
}

int bili_parse_feed(const char *json, struct bili_video *out, int max)
{
	int n = 0;

	for (const char *it = js_first(js_path(json, "data.item")); it != NULL && n < max;
	     it = js_next(it)) {
		if (parse_video(it, &out[n]) == 0) {
			n++;
		}
	}

	return n;
}

int bili_parse_popular(const char *json, struct bili_video *out, int max)
{
	int n = 0;

	for (const char *it = js_first(js_path(json, "data.list")); it != NULL && n < max;
	     it = js_next(it)) {
		struct bili_video *v = &out[n];

		if (js_str(js_get(it, "bvid"), v->bvid, sizeof(v->bvid)) <= 0 ||
		    js_str(js_get(it, "title"), v->title, sizeof(v->title)) < 0) {
			continue;
		}
		fill_common(it, v);
		v->duration = js_int(js_get(it, "duration"), 0);
		if (v->cid != 0) {
			n++;
		}
	}

	return n;
}

int bili_parse_search(const char *json, struct bili_video *out, int max, int *pages)
{
	int n = 0;

	if (pages != NULL) {
		*pages = num(js_path(json, "data.numPages"), 0);
	}
	for (const char *it = js_first(js_path(json, "data.result")); it != NULL && n < max;
	     it = js_next(it)) {
		struct bili_video *v = &out[n];
		char dur[16];

		if (js_str(js_get(it, "bvid"), v->bvid, sizeof(v->bvid)) <= 0 ||
		    js_str(js_get(it, "title"), v->title, sizeof(v->title)) < 0) {
			continue;
		}
		bili_strip_html(v->title);
		fill_common(it, v);
		v->cid = 0;
		v->duration = js_str(js_get(it, "duration"), dur, sizeof(dur)) > 0 ?
			      bili_parse_clock(dur) : 0;
		n++;
	}

	return n;
}

int bili_parse_hot_words(const char *json, char words[][BILI_HOT_WORD_LEN], int max)
{
	int n = 0;

	for (const char *it = js_first(js_path(json, "data.trending.list")); it != NULL && n < max;
	     it = js_next(it)) {
		if (js_str(js_get(it, "keyword"), words[n], BILI_HOT_WORD_LEN) > 0) {
			n++;
		}
	}

	return n;
}

int bili_parse_view_cid(const char *json, uint64_t *cid)
{
	long long v = js_int(js_get(js_first(js_get(json, "data")), "cid"), 0);

	if (v == 0) {
		v = js_int(js_path(json, "data.cid"), 0);
	}
	*cid = v;

	return v != 0 ? 0 : -ENOENT;
}

/* full width Latin letters, digits and punctuation (U+FF01..FF5E) and the ideographic space become the
 * ordinary ones: the font draws them at the width of a Chinese character, which ruins the look of a title */
void bili_fold_width(char *s)
{
	char *w = s;

	while (*s != '\0') {
		if ((uint8_t)s[0] == 0xef && ((uint8_t)s[1] == 0xbc || (uint8_t)s[1] == 0xbd) &&
		    ((uint8_t)s[2] & 0xc0) == 0x80) {
			unsigned int cp = 0xff00 + ((uint8_t)s[1] == 0xbd ? 0x40 : 0) + ((uint8_t)s[2] - 0x80);

			if (cp >= 0xff01 && cp <= 0xff5e) {
				*w++ = (char)(cp - 0xff00 + 0x20);
				s += 3;
				continue;
			}
		}
		if ((uint8_t)s[0] == 0xe3 && (uint8_t)s[1] == 0x80 && (uint8_t)s[2] == 0x80) {
			*w++ = ' ';
			s += 3;
			continue;
		}
		*w++ = *s++;
	}
	*w = '\0';
}


void bili_strip_html(char *s)
{
	static const struct {
		const char *ent;
		char ch;
	} ents[] = {{"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&#39;", '\''},
		    {"&apos;", '\''}};
	char *w = s;

	while (*s != '\0') {
		if (*s == '<') {
			const char *end = strchr(s, '>');

			if (end != NULL) {
				s = (char *)end + 1;
				continue;
			}
		}
		if (*s == '&') {
			size_t i;

			for (i = 0; i < sizeof(ents) / sizeof(ents[0]); i++) {
				size_t l = strlen(ents[i].ent);

				if (strncmp(s, ents[i].ent, l) == 0) {
					*w++ = ents[i].ch;
					s += l;
					break;
				}
			}
			if (i < sizeof(ents) / sizeof(ents[0])) {
				continue;
			}
		}
		*w++ = *s++;
	}
	*w = '\0';
}

uint32_t bili_parse_clock(const char *s)
{
	uint32_t total = 0, cur = 0;
	int digits = 0;

	for (; *s != '\0'; s++) {
		if (*s >= '0' && *s <= '9') {
			cur = cur * 10 + (*s - '0');
			digits++;
		} else if (*s == ':' && digits > 0) {
			total = total * 60 + cur;
			cur = 0;
			digits = 0;
		} else {
			return 0;
		}
	}

	return digits > 0 ? total * 60 + cur : 0;
}

int bili_cover_url(const char *pic, int w, int h, char *out, size_t size)
{
	const char *p = pic;
	int n;

	if (strncmp(p, "https://", 8) == 0) {
		p += 8;
	} else if (strncmp(p, "http://", 7) == 0) {
		p += 7;
	} else if (strncmp(p, "//", 2) == 0) {
		p += 2;
	} else {
		return -EINVAL;
	}
	if (strchr(p, '@') != NULL) {
		n = snprintf(out, size, "http://%s", p);
	} else {
		n = snprintf(out, size, "http://%s@%dw_%dh_1c.jpg", p, w, h);
	}

	return n >= (int)size ? -ENOSPC : n;
}

int bili_parse_playurl(const char *json, struct bili_stream *out)
{
	const char *d = js_first(js_path(json, "data.durl"));

	if (d == NULL || js_str(js_get(d, "url"), out->url, sizeof(out->url)) <= 0) {
		return -ENOENT;
	}
	out->size = js_int(js_get(d, "size"), 0);
	out->length_ms = js_int(js_get(d, "length"), 0);
	out->quality = js_int(js_path(json, "data.quality"), 0);

	return 0;
}

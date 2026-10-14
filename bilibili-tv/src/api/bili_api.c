/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "api/account.h"
#include "api/bili_api.h"
#include "net/http.h"
#include "net/inflate.h"
#include "net/json.h"
#include "net/wbi.h"

#define API		"https://api.bilibili.com"
#define ANSWER_MAX	(128 * 1024)
#define QN_360P		16

#define CODE_RISK	(-412)	/* too many requests / no browser identity */
#define CODE_SIGN_1	(-403)	/* the signature or its key is wrong */
#define CODE_SIGN_2	(-352)

static char wbi_key[33];
/* "buvid3=...; buvid4=..." once the server has asked for a browser identity */
static char buvid[200];
/* the whole Cookie header of a request: the browser identity and, when logged in, the login */
static char cookie_hdr[760];

static const char *headers(void)
{
	char acc[460];

	account_cookie(acc, sizeof(acc));
	if (buvid[0] == '\0' && acc[0] == '\0') {
		return NULL;
	}
	snprintf(cookie_hdr, sizeof(cookie_hdr), "Cookie: %s%s%s\r\n", buvid, buvid[0] && acc[0] ? "; " : "", acc);

	return cookie_hdr;
}

/*
 * The server does not check the time stamp of a signed request, so the clock of the board, which
 * has no network time, does not have to be right.
 */
static int64_t stamp(void)
{
	return 1700000000 + k_uptime_get() / 1000;
}

/* one request: the whole answer in a new buffer, NULL when the transfer failed; @p res gets the cookies set */
static char *fetch_res(const char *url, struct http_result *res)
{
	struct http_opts o = {
		.range_start = -1,
		.tls_verify = IS_ENABLED(CONFIG_BILI_TLS_VERIFY),
		.headers = headers(),
	};
	char *buf = malloc(ANSWER_MAX);
	int n;

	if (buf == NULL) {
		return NULL;
	}
	n = http_get_text(url, &o, buf, ANSWER_MAX, res);
	if (n < 0) {
		printk("api: %s failed (%d)\n", url + sizeof(API) - 1, n);
		free(buf);
		return NULL;
	}

	return buf;
}

static char *fetch(const char *url)
{
	return fetch_res(url, NULL);
}

/* the signing key comes with the nav answer, which needs no login (the answer is code -101) */
static int load_key(void)
{
	char *a = fetch(API "/x/web-interface/nav");
	int ret;

	if (a == NULL) {
		return -EIO;
	}
	ret = bili_parse_nav(a, wbi_key);
	free(a);
	if (ret != 0) {
		printk("api: no signing key in the nav answer\n");
	}

	return ret;
}

/* the browser identity: two random looking ids that the search wants as cookies */
static int load_cookie(void)
{
	char *a = fetch(API "/x/frontend/finger/spi");
	char b3[80], b4[96];
	int ret = -EBADMSG;

	if (a == NULL) {
		return -EIO;
	}
	if (js_str(js_path(a, "data.b_3"), b3, sizeof(b3)) > 0 &&
	    js_str(js_path(a, "data.b_4"), b4, sizeof(b4)) > 0) {
		snprintf(buvid, sizeof(buvid), "buvid3=%s; buvid4=%s", b3, b4);
		ret = 0;
	}
	free(a);

	return ret;
}

/*
 * A signed call. Returns the answer with code 0, or NULL (the code is in *code, 0 when there was
 * no answer). A refused signature gets a new key and a risk answer gets the cookie, once each.
 */
static char *call(const char *path, const char *const *params, int count, int *code)
{
	bool new_key = false, new_cookie = false;

	for (;;) {
		char url[800], query[600], *a;
		int ret;

		*code = 0;
		if (wbi_key[0] == '\0' && load_key() != 0) {
			return NULL;
		}
		ret = wbi_query(params, count, wbi_key, stamp(), query, sizeof(query));
		if (ret < 0) {
			return NULL;
		}
		snprintf(url, sizeof(url), API "%s?%s", path, query);
		a = fetch(url);
		if (a == NULL) {
			return NULL;
		}
		*code = bili_parse_code(a);
		if (*code == 0) {
			return a;
		}
		free(a);
		printk("api: %s answered code %d\n", path, *code);
		if ((*code == CODE_SIGN_1 || *code == CODE_SIGN_2) && !new_key) {
			new_key = true;
			wbi_key[0] = '\0';
		} else if (*code == CODE_RISK && !new_cookie) {
			new_cookie = true;
			if (load_cookie() != 0) {
				return NULL;
			}
		} else {
			return NULL;
		}
	}
}

int bili_api_feed(int page, struct bili_video *out, int max)
{
	char ps[16], idx[24];
	const char *params[] = {ps, "fresh_type=4", idx, "web_location=1430650"};
	char *a;
	int n, code;

	snprintf(ps, sizeof(ps), "ps=%d", max);
	snprintf(idx, sizeof(idx), "fresh_idx=%d", page);
	a = call("/x/web-interface/wbi/index/top/feed/rcmd", params, ARRAY_SIZE(params), &code);
	if (a == NULL) {
		return code != 0 ? -EACCES : -EIO;
	}
	n = bili_parse_feed(a, out, max);
	free(a);

	return n;
}

int bili_api_popular(int page, struct bili_video *out, int max)
{
	char ps[16], pn[16];
	const char *params[] = {ps, pn};
	char *a;
	int n, code;

	snprintf(ps, sizeof(ps), "ps=%d", max);
	snprintf(pn, sizeof(pn), "pn=%d", page);
	a = call("/x/web-interface/popular", params, ARRAY_SIZE(params), &code);
	if (a == NULL) {
		return code != 0 ? -EACCES : -EIO;
	}
	n = bili_parse_popular(a, out, max);
	free(a);

	return n;
}

int bili_api_search(const char *keyword, int page, struct bili_video *out, int max, int *pages)
{
	char kw[128], pg[16];
	const char *params[] = {"search_type=video", kw, pg};
	char *a;
	int n, code;

	snprintf(kw, sizeof(kw), "keyword=%s", keyword);
	snprintf(pg, sizeof(pg), "page=%d", page);
	a = call("/x/web-interface/wbi/search/type", params, ARRAY_SIZE(params), &code);
	if (a == NULL) {
		return code != 0 ? -EACCES : -EIO;
	}
	n = bili_parse_search(a, out, max, pages);
	free(a);

	return n;
}

int bili_api_hot_words(char words[][BILI_HOT_WORD_LEN], int max)
{
	const char *params[] = {"limit=10"};
	char *a;
	int n, code;

	a = call("/x/web-interface/wbi/search/square", params, ARRAY_SIZE(params), &code);
	if (a == NULL) {
		return code != 0 ? -EACCES : -EIO;
	}
	n = bili_parse_hot_words(a, words, max);
	free(a);

	return n;
}

/* the cid of a video that came without one */
static int resolve_cid(const char *bvid, uint64_t *cid)
{
	char b[32];
	const char *params[] = {b};
	char *a;
	int ret, code;

	snprintf(b, sizeof(b), "bvid=%s", bvid);
	a = call("/x/player/pagelist", params, ARRAY_SIZE(params), &code);
	if (a == NULL) {
		return code != 0 ? -EACCES : -EIO;
	}
	ret = bili_parse_view_cid(a, cid);
	free(a);

	return ret;
}

int bili_api_stream(const struct bili_video *v, struct bili_stream *out)
{
	char bvid[32], cid[32], qn[16];
	const char *params[] = {bvid, cid, qn, "fnval=0", "fnver=0", "fourk=0", "platform=html5",
				"high_quality=1"};
	uint64_t id = v->cid;
	char *a;
	int ret, code;

	if (id == 0) {
		ret = resolve_cid(v->bvid, &id);
		if (ret != 0) {
			return ret;
		}
	}
	snprintf(bvid, sizeof(bvid), "bvid=%s", v->bvid);
	snprintf(cid, sizeof(cid), "cid=%llu", (unsigned long long)id);
	snprintf(qn, sizeof(qn), "qn=%d", QN_360P);
	a = call("/x/player/wbi/playurl", params, ARRAY_SIZE(params), &code);
	if (a == NULL) {
		return code != 0 ? -EACCES : -EIO;
	}
	ret = bili_parse_playurl(a, out);
	out->cid = id;
	free(a);
	if (ret == 0 && !IS_ENABLED(CONFIG_BILI_CDN_HTTPS) && strncmp(out->url, "https://", 8) == 0) {
		/* the video servers answer on port 80 as well, without the cost of the encryption */
		memmove(out->url + 4, out->url + 5, strlen(out->url + 5) + 1);
	}

	return ret;
}

/* ---- the comments -------------------------------------------------------------------------- */

struct dm_sink {
	struct inflate *inf;
	struct dm_parser *parser;
	bool started, plain;
	int status;
};

/* decoded bytes of the XML */
static int dm_xml(void *ctx, const uint8_t *d, size_t n)
{
	return dm_parser_feed(ctx, d, n);
}

/* the bytes of the answer: raw deflate as the server sends it, or the XML itself */
static int dm_body(void *ctx, const uint8_t *d, size_t n)
{
	struct dm_sink *s = ctx;
	int ret;

	if (!s->started) {
		s->started = true;
		s->plain = n > 0 && d[0] == '<';
	}
	if (s->plain) {
		return dm_parser_feed(s->parser, d, n);
	}
	ret = inflate_feed(s->inf, d, n);

	return ret < 0 ? ret : 0;	/* 1: the stream is complete, what follows is of no interest */
}

int bili_api_danmaku(uint64_t cid, struct dm_set *set)
{
	char url[96];
	struct dm_sink sink = {0};
	struct http_result r;
	struct http_opts o = {.range_start = -1, .tls_verify = IS_ENABLED(CONFIG_BILI_TLS_VERIFY), .timeout_ms = 15000,
			      .headers = headers()};
	int ret;

	snprintf(url, sizeof(url), API "/x/v1/dm/list.so?oid=%llu", (unsigned long long)cid);
	sink.parser = dm_parser_new(set);
	sink.inf = inflate_new(dm_xml, sink.parser);
	if (sink.parser == NULL || sink.inf == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	ret = http_get(url, &o, dm_body, &sink, &r);
	if (ret == 0 && r.status / 100 != 2) {
		ret = -EIO;
	}
	if (ret != 0) {
		printk("api: danmaku of %llu failed (%d), %u read\n", (unsigned long long)cid, ret, set->count);
	}
out:
	if (sink.parser != NULL) {
		dm_parser_finish(sink.parser);
	}
	dm_parser_free(sink.parser);
	inflate_free(sink.inf);

	return set->count > 0 || ret == 0 ? (int)set->count : ret;
}

/* ---- the login ----------------------------------------------------------------------------- */

#define PASSPORT	"https://passport.bilibili.com"

int bili_api_qr_new(char *url, size_t url_size, char *key, size_t key_size)
{
	char *a = fetch(PASSPORT "/x/passport-login/web/qrcode/generate");
	int ret;

	if (a == NULL) {
		return -EIO;
	}
	ret = bili_parse_code(a) == 0 ? bili_parse_qr_new(a, url, url_size, key, key_size) : -EACCES;
	free(a);

	return ret;
}

int bili_api_qr_poll(const char *key, int *state)
{
	char path[160], *a;
	int ret;

	snprintf(path, sizeof(path), PASSPORT "/x/passport-login/web/qrcode/poll?qrcode_key=%s", key);
	struct http_result *r = malloc(sizeof(*r));

	if (r == NULL) {
		return -ENOMEM;
	}
	a = fetch_res(path, r);
	if (a == NULL) {
		free(r);
		return -EIO;
	}
	ret = bili_parse_qr_poll(a, state, NULL, 0);
	if (ret == 0 && *state == 0) {
		/* the cookies are in the address of the answer, or set by the server; the code is used up by this answer */
		ret = account_set_from_text(a);
		if (ret != 0 && r->set_cookie[0] != '\0') {
			ret = account_set_from_text(r->set_cookie);
		}
		if (ret != 0) {
			*state = -1;
		}
	}
	free(a);
	free(r);

	return ret;
}

int bili_api_user(char *name, size_t size, char *face, size_t face_size)
{
	char *a = fetch(API "/x/web-interface/nav");
	int ret;

	if (a == NULL) {
		return -EIO;
	}
	ret = bili_parse_user(a, name, size, face, face_size);
	free(a);

	return ret;
}

/* ---- the followed users -------------------------------------------------------------------- */

static char follow_offset[40];

int bili_api_follow(int page, struct bili_video *out, int max, bool *more)
{
	char url[220], *a;
	int n;
	bool has_more = false;

	if (!account_logged_in()) {
		return -EACCES;
	}
	if (page == 1) {
		follow_offset[0] = '\0';
	}
	snprintf(url, sizeof(url), API "/x/polymer/web-dynamic/v1/feed/all?type=video&timezone_offset=-480&offset=%s",
		 follow_offset);
	a = fetch(url);
	if (a == NULL) {
		return -EIO;
	}
	if (bili_parse_code(a) != 0) {
		printk("api: follow feed answered code %d\n", bili_parse_code(a));
		free(a);
		return -EACCES;
	}
	n = bili_parse_dynamic(a, out, max, follow_offset, sizeof(follow_offset), &has_more);
	free(a);
	if (more != NULL) {
		*more = has_more;
	}

	return n;
}

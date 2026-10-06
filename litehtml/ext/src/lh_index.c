/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * Splits a big page into pieces that can be laid out one at a time. The page is read once,
 * in blocks, through a tag scanner that keeps a depth count; a piece ends where a child of
 * <body> ends. The scanner needs well formed markup (end tags of the elements that are
 * not void) and knows nothing of the rules that close elements by themselves.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <litehtml_zephyr/lh.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define BLOCK		4096
#define TAGBUF		256

enum state {
	S_TEXT,
	S_LT,		/* after '<' */
	S_NAME,		/* the name of a tag */
	S_ATTRS,	/* inside a tag, after the name */
	S_QUOTE,	/* inside a quoted attribute value */
	S_BANG,		/* <! or <? : up to '>' */
	S_COMMENT,	/* <!-- ... --> */
	S_RAW,		/* the text of <script> or <style> */
};

struct scan {
	enum state st;
	uint32_t pos;
	uint32_t tag_start;
	char name[16];
	int name_len;
	bool closing;
	bool self_close;
	char quote;
	char last;		/* last character of a tag that was not a space */
	char tag[TAGBUF];
	int tag_len;
	int depth;
	int body_depth;
	bool in_body;
	bool done;
	uint32_t chunk_start;
	uint32_t body_end;
	/* script / style text */
	char raw[8];
	int raw_len;
	int raw_match;
	bool raw_css;
	uint32_t raw_start;
	/* comments and <! */
	int bang_len;
	char c1, c2;
};

static char lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static bool is_alpha(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool is_name_char(char c)
{
	return is_alpha(c) || (c >= '0' && c <= '9') || c == '-' || c == ':';
}

static bool is_void(const char *n)
{
	static const char *const v[] = {"area", "base", "br", "col", "embed", "hr", "img", "input",
					"link", "meta", "param", "source", "track", "wbr"};

	for (unsigned int i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		if (strcmp(n, v[i]) == 0) {
			return true;
		}
	}
	return false;
}

struct ctx {
	struct scan s;
	struct lh_index *out;
	uint32_t target;
	int max_chunks;
	int n;
};

static void push_chunk(struct ctx *c, uint32_t end)
{
	if (c->n < c->max_chunks && end > c->s.chunk_start) {
		c->out->chunks[c->n].offset = c->s.chunk_start;
		c->out->chunks[c->n].length = end - c->s.chunk_start;
		c->n++;
	}
	c->s.chunk_start = end;
}

/* a child of <body> is complete, ending right before 'end' */
static void child_done(struct ctx *c, uint32_t end)
{
	if (end - c->s.chunk_start >= c->target) {
		push_chunk(c, end);
	}
}

static void tag_end(struct ctx *c)
{
	struct scan *s = &c->s;
	uint32_t end = s->pos + 1;

	s->name[s->name_len] = '\0';
	if (!s->closing) {
		bool v = is_void(s->name) || s->self_close;

		if (strcmp(s->name, "body") == 0 && !s->in_body) {
			size_t n = MIN(s->tag_len, (int)sizeof(c->out->body_tag) - 1);

			memcpy(c->out->body_tag, s->tag, n);
			c->out->body_tag[n] = '\0';
			s->in_body = true;
			s->depth++;
			s->body_depth = s->depth;
			s->chunk_start = end;
		} else if (v) {
			if (s->in_body && s->depth == s->body_depth) {
				child_done(c, end);
			}
		} else {
			s->depth++;
			if (strcmp(s->name, "script") == 0 || strcmp(s->name, "style") == 0) {
				s->raw_css = (s->name[1] == 't');
				s->raw_start = end;
				strcpy(s->raw, s->name);
				s->raw_len = (int)strlen(s->raw);
				s->raw_match = 0;
				s->st = S_RAW;
				return;
			}
		}
	} else {
		if (strcmp(s->name, "body") == 0 && s->in_body) {
			s->body_end = s->tag_start;
			s->done = true;
			return;
		}
		if (s->depth > 0) {
			s->depth--;
		}
		if (s->in_body && s->depth == s->body_depth) {
			child_done(c, end);
		}
	}
	s->st = S_TEXT;
}

static void feed(struct ctx *c, char ch)
{
	struct scan *s = &c->s;

	if (s->tag_len < TAGBUF - 1 && s->st != S_TEXT && s->st != S_RAW) {
		s->tag[s->tag_len++] = ch;
	}
	switch (s->st) {
	case S_TEXT:
		if (ch == '<') {
			s->st = S_LT;
			s->tag_start = s->pos;
			s->tag_len = 0;
			s->tag[s->tag_len++] = ch;
			s->closing = false;
			s->self_close = false;
			s->name_len = 0;
		}
		break;
	case S_LT:
		if (ch == '/') {
			s->closing = true;
			s->st = S_NAME;
		} else if (ch == '!' || ch == '?') {
			s->st = S_BANG;
			s->bang_len = 0;
			s->c1 = s->c2 = 0;
			s->bang_len = (ch == '!') ? 1 : 0;
		} else if (is_alpha(ch)) {
			s->name[s->name_len++] = lower(ch);
			s->st = S_NAME;
		} else {
			s->st = S_TEXT;
		}
		break;
	case S_NAME:
		if (is_name_char(ch)) {
			if (s->name_len < (int)sizeof(s->name) - 1) {
				s->name[s->name_len++] = lower(ch);
			}
		} else if (ch == '>') {
			s->last = 0;
			tag_end(c);
		} else {
			s->last = ch;
			s->st = S_ATTRS;
		}
		break;
	case S_ATTRS:
		if (ch == '"' || ch == '\'') {
			s->quote = ch;
			s->st = S_QUOTE;
		} else if (ch == '>') {
			s->self_close = (s->last == '/');
			tag_end(c);
		} else if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
			s->last = ch;
		}
		break;
	case S_QUOTE:
		if (ch == s->quote) {
			s->last = ch;
			s->st = S_ATTRS;
		}
		break;
	case S_BANG:
		/* "<!--" starts a comment, anything else ends at the next '>' */
		s->bang_len++;
		if (s->bang_len == 2) {
			s->c1 = ch;
		} else if (s->bang_len == 3) {
			s->c2 = ch;
			if (s->c1 == '-' && s->c2 == '-') {
				s->c1 = s->c2 = 0;
				s->st = S_COMMENT;
				break;
			}
		}
		if (ch == '>') {
			s->st = S_TEXT;
		}
		break;
	case S_COMMENT:
		if (ch == '>' && s->c1 == '-' && s->c2 == '-') {
			s->st = S_TEXT;
		}
		s->c1 = s->c2;
		s->c2 = ch;
		break;
	case S_RAW: {
		/* up to "</name" */
		static const char pre[] = "</";

		if (s->raw_match < 2) {
			s->raw_match = (ch == pre[s->raw_match]) ? s->raw_match + 1 : (ch == '<');
		} else if (lower(ch) == s->raw[s->raw_match - 2]) {
			s->raw_match++;
			if (s->raw_match - 2 == s->raw_len) {
				/* the end tag: read its '>' as a normal closing tag */
				s->closing = true;
				s->name_len = s->raw_len;
				memcpy(s->name, s->raw, s->raw_len);
				s->tag_start = s->pos - (uint32_t)(s->raw_len + 1);
				if (s->raw_css && !s->in_body && c->out->n_css < LH_INDEX_MAX_CSS) {
					c->out->css[c->out->n_css].offset = s->raw_start;
					c->out->css[c->out->n_css].length = s->tag_start - s->raw_start;
					c->out->n_css++;
				}
				s->tag_len = 0;
				s->last = 0;
				s->st = S_ATTRS;
			}
		} else {
			s->raw_match = (ch == '<');
		}
		break;
	}
	}
}

int lh_index_scan(lh_read_fn read, void *user, uint32_t size, uint32_t target, struct lh_index *out)
{
	static uint8_t block[BLOCK];
	struct ctx c = {.out = out, .target = target, .max_chunks = LH_INDEX_MAX_CHUNKS};
	uint32_t off = 0;

	memset(out, 0, sizeof(*out));
	while (off < size && !c.s.done) {
		uint32_t n = MIN((uint32_t)BLOCK, size - off);

		if (read(user, off, block, n) != 0) {
			return -1;
		}
		for (uint32_t i = 0; i < n && !c.s.done; i++) {
			c.s.pos = off + i;
			feed(&c, (char)block[i]);
		}
		off += n;
	}
	if (!c.s.in_body) {
		/* a fragment without <body>: one piece */
		out->chunks[0].offset = 0;
		out->chunks[0].length = size;
		out->n_chunks = 1;
		out->body_tag[0] = '\0';
		return 1;
	}
	{
		uint32_t end = c.s.done ? c.s.body_end : size;

		/* what is left after the last piece is joined to it when it is small */
		if (c.n > 0 && end - c.s.chunk_start < target / 4) {
			out->chunks[c.n - 1].length += end - c.s.chunk_start;
			c.s.chunk_start = end;
		} else {
			push_chunk(&c, end);
		}
	}
	if (c.n == 0) {
		out->chunks[0].offset = c.s.chunk_start;
		out->chunks[0].length = 0;
		c.n = 1;
	}
	out->n_chunks = c.n;
	return c.n;
}

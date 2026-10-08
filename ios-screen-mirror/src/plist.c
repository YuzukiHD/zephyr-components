/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <plist/plist.h>

enum kind {
	K_NULL, K_BOOL, K_UINT, K_REAL, K_STRING, K_DATA, K_ARRAY, K_DICT,
};

struct plist_node {
	enum kind kind;
	uint64_t num;			/* bool, uint */
	double real;
	uint8_t *buf;			/* string (with its NUL), data */
	uint64_t len;
	struct plist_node **items;	/* array items, dict values */
	char **keys;			/* dict */
	uint32_t count;
	uint32_t cap;
};

static plist_t node_new(enum kind kind)
{
	plist_t n = calloc(1, sizeof(*n));

	if (n != NULL) {
		n->kind = kind;
	}

	return n;
}

plist_t plist_new_dict(void)
{
	return node_new(K_DICT);
}

plist_t plist_new_array(void)
{
	return node_new(K_ARRAY);
}

plist_t plist_new_string(const char *val)
{
	plist_t n = node_new(K_STRING);

	if (n != NULL) {
		n->len = strlen(val);
		n->buf = malloc(n->len + 1);
		if (n->buf == NULL) {
			free(n);
			return NULL;
		}
		memcpy(n->buf, val, n->len + 1);
	}

	return n;
}

plist_t plist_new_bool(uint8_t val)
{
	plist_t n = node_new(K_BOOL);

	if (n != NULL) {
		n->num = val != 0;
	}

	return n;
}

plist_t plist_new_uint(uint64_t val)
{
	plist_t n = node_new(K_UINT);

	if (n != NULL) {
		n->num = val;
	}

	return n;
}

plist_t plist_new_real(double val)
{
	plist_t n = node_new(K_REAL);

	if (n != NULL) {
		n->real = val;
	}

	return n;
}

plist_t plist_new_data(const char *val, uint64_t length)
{
	plist_t n = node_new(K_DATA);

	if (n != NULL) {
		n->buf = malloc(length != 0 ? length : 1);
		if (n->buf == NULL) {
			free(n);
			return NULL;
		}
		memcpy(n->buf, val, length);
		n->len = length;
	}

	return n;
}

static bool grow(plist_t n)
{
	if (n->count < n->cap) {
		return true;
	}

	uint32_t cap = n->cap != 0 ? n->cap * 2 : 4;
	plist_t *items = realloc(n->items, cap * sizeof(*items));

	if (items == NULL) {
		return false;
	}
	n->items = items;
	if (n->kind == K_DICT) {
		char **keys = realloc(n->keys, cap * sizeof(*keys));

		if (keys == NULL) {
			return false;
		}
		n->keys = keys;
	}
	n->cap = cap;

	return true;
}

plist_type plist_get_node_type(plist_t node)
{
	static const plist_type map[] = {
		[K_NULL] = PLIST_NONE,   [K_BOOL] = PLIST_BOOLEAN, [K_UINT] = PLIST_UINT,
		[K_REAL] = PLIST_REAL,   [K_STRING] = PLIST_STRING, [K_DATA] = PLIST_DATA,
		[K_ARRAY] = PLIST_ARRAY, [K_DICT] = PLIST_DICT,
	};

	return node != NULL ? map[node->kind] : PLIST_NONE;
}

void plist_array_append_item(plist_t node, plist_t item)
{
	if (node == NULL || item == NULL || node->kind != K_ARRAY || !grow(node)) {
		return;
	}
	node->items[node->count++] = item;
}

plist_t plist_array_get_item(plist_t node, uint32_t n)
{
	return (node != NULL && node->kind == K_ARRAY && n < node->count) ? node->items[n] : NULL;
}

uint32_t plist_array_get_size(plist_t node)
{
	return (node != NULL && node->kind == K_ARRAY) ? node->count : 0;
}

void plist_dict_set_item(plist_t node, const char *key, plist_t item)
{
	if (node == NULL || item == NULL || node->kind != K_DICT) {
		return;
	}
	for (uint32_t i = 0; i < node->count; i++) {
		if (strcmp(node->keys[i], key) == 0) {
			plist_free(node->items[i]);
			node->items[i] = item;
			return;
		}
	}
	if (!grow(node)) {
		return;
	}
	node->keys[node->count] = malloc(strlen(key) + 1);
	if (node->keys[node->count] == NULL) {
		return;
	}
	strcpy(node->keys[node->count], key);
	node->items[node->count++] = item;
}

plist_t plist_dict_get_item(plist_t node, const char *key)
{
	if (node == NULL || node->kind != K_DICT) {
		return NULL;
	}
	for (uint32_t i = 0; i < node->count; i++) {
		if (strcmp(node->keys[i], key) == 0) {
			return node->items[i];
		}
	}

	return NULL;
}

void plist_get_uint_val(plist_t node, uint64_t *val)
{
	*val = (node != NULL && (node->kind == K_UINT || node->kind == K_BOOL)) ? node->num : 0;
}

void plist_get_data_val(plist_t node, char **val, uint64_t *length)
{
	*val = NULL;
	*length = 0;
	if (node == NULL || (node->kind != K_DATA && node->kind != K_STRING)) {
		return;
	}
	*val = malloc(node->len + 1);
	if (*val != NULL) {
		memcpy(*val, node->buf, node->len);
		(*val)[node->len] = '\0';
		*length = node->len;
	}
}

void plist_free(plist_t node)
{
	if (node == NULL) {
		return;
	}
	for (uint32_t i = 0; i < node->count; i++) {
		plist_free(node->items[i]);
		if (node->kind == K_DICT) {
			free(node->keys[i]);
		}
	}
	free(node->items);
	free(node->keys);
	free(node->buf);
	free(node);
}

/* Binary reader */

struct reader {
	const uint8_t *p;
	uint32_t len;
	uint32_t offset_table;
	uint8_t offset_size;
	uint8_t ref_size;
	uint64_t objects;
};

static bool be_read(const uint8_t *p, unsigned int size, uint64_t *out)
{
	uint64_t v = 0;

	if (size == 0U || size > 8U) {
		return false;
	}
	for (unsigned int i = 0; i < size; i++) {
		v = (v << 8) | p[i];
	}
	*out = v;

	return true;
}

static plist_t read_object(const struct reader *r, uint64_t index, unsigned int depth);

static bool read_size(const struct reader *r, uint32_t *pos, uint8_t low, uint64_t *size)
{
	if (low != 0x0fU) {
		*size = low;
		return true;
	}
	if (*pos >= r->len) {
		return false;
	}

	uint8_t m = r->p[(*pos)++];
	unsigned int bytes = 1U << (m & 0x0f);

	if ((m >> 4) != 1U || *pos + bytes > r->len) {
		return false;
	}
	if (!be_read(r->p + *pos, bytes, size)) {
		return false;
	}
	*pos += bytes;

	return true;
}

static plist_t read_object(const struct reader *r, uint64_t index, unsigned int depth)
{
	uint64_t off, size;
	uint32_t pos;
	uint8_t marker, high, low;
	plist_t n = NULL;

	if (index >= r->objects || depth > 16U ||
	    !be_read(r->p + r->offset_table + index * r->offset_size, r->offset_size, &off) ||
	    off >= r->len) {
		return NULL;
	}
	pos = off;
	marker = r->p[pos++];
	high = marker >> 4;
	low = marker & 0x0f;

	switch (high) {
	case 0x0:
		n = node_new(K_BOOL);
		if (n != NULL) {
			n->num = (low == 0x9U);
		}
		return n;
	case 0x1: {
		unsigned int bytes = 1U << low;
		uint64_t v;

		if (bytes > 16U || pos + bytes > r->len) {
			return NULL;
		}
		/* 16 byte integers are only used for values of 2^63 and up */
		if (!be_read(r->p + pos + (bytes == 16U ? 8 : 0), bytes == 16U ? 8 : bytes, &v)) {
			return NULL;
		}
		return plist_new_uint(v);
	}
	case 0x2: {
		unsigned int bytes = 1U << low;
		uint64_t v;

		if ((bytes != 4U && bytes != 8U) || pos + bytes > r->len ||
		    !be_read(r->p + pos, bytes, &v)) {
			return NULL;
		}
		if (bytes == 8U) {
			double d;

			memcpy(&d, &v, sizeof(d));
			return plist_new_real(d);
		} else {
			float f;
			uint32_t v32 = v;

			memcpy(&f, &v32, sizeof(f));
			return plist_new_real(f);
		}
	}
	case 0x4:
	case 0x5:
		if (!read_size(r, &pos, low, &size) || pos + size > r->len) {
			return NULL;
		}
		if (high == 0x4U) {
			return plist_new_data((const char *)r->p + pos, size);
		}
		n = node_new(K_STRING);
		if (n != NULL) {
			n->buf = malloc(size + 1);
			if (n->buf == NULL) {
				free(n);
				return NULL;
			}
			memcpy(n->buf, r->p + pos, size);
			n->buf[size] = '\0';
			n->len = size;
		}
		return n;
	case 0x6: {
		/* UTF-16 (big endian), kept as UTF-8 */
		uint32_t o = 0;

		if (!read_size(r, &pos, low, &size) || pos + size * 2 > r->len) {
			return NULL;
		}
		n = node_new(K_STRING);
		if (n == NULL || (n->buf = malloc(size * 4 + 1)) == NULL) {
			free(n);
			return NULL;
		}
		for (uint64_t i = 0; i < size; i++) {
			uint32_t c = (r->p[pos + i * 2] << 8) | r->p[pos + i * 2 + 1];

			if (c >= 0xd800U && c < 0xdc00U && i + 1 < size) {
				uint32_t lo = (r->p[pos + i * 2 + 2] << 8) | r->p[pos + i * 2 + 3];

				c = 0x10000U + ((c - 0xd800U) << 10) + (lo - 0xdc00U);
				i++;
			}
			if (c < 0x80U) {
				n->buf[o++] = c;
			} else if (c < 0x800U) {
				n->buf[o++] = 0xc0 | (c >> 6);
				n->buf[o++] = 0x80 | (c & 0x3f);
			} else if (c < 0x10000U) {
				n->buf[o++] = 0xe0 | (c >> 12);
				n->buf[o++] = 0x80 | ((c >> 6) & 0x3f);
				n->buf[o++] = 0x80 | (c & 0x3f);
			} else {
				n->buf[o++] = 0xf0 | (c >> 18);
				n->buf[o++] = 0x80 | ((c >> 12) & 0x3f);
				n->buf[o++] = 0x80 | ((c >> 6) & 0x3f);
				n->buf[o++] = 0x80 | (c & 0x3f);
			}
		}
		n->buf[o] = '\0';
		n->len = o;
		return n;
	}
	case 0xa:
	case 0xd: {
		uint64_t count;

		if (!read_size(r, &pos, low, &count)) {
			return NULL;
		}
		uint64_t refs = high == 0xdU ? count * 2 : count;

		if (refs > r->len || pos + refs * r->ref_size > r->len) {
			return NULL;
		}
		n = node_new(high == 0xdU ? K_DICT : K_ARRAY);
		if (n == NULL) {
			return NULL;
		}
		for (uint64_t i = 0; i < count; i++) {
			uint64_t kref = 0, vref;
			plist_t key = NULL, val;

			if (high == 0xdU) {
				be_read(r->p + pos + i * r->ref_size, r->ref_size, &kref);
				be_read(r->p + pos + (count + i) * r->ref_size, r->ref_size, &vref);
				key = read_object(r, kref, depth + 1);
				if (key == NULL || key->kind != K_STRING) {
					plist_free(key);
					plist_free(n);
					return NULL;
				}
			} else {
				be_read(r->p + pos + i * r->ref_size, r->ref_size, &vref);
			}
			val = read_object(r, vref, depth + 1);
			if (val == NULL) {
				plist_free(key);
				plist_free(n);
				return NULL;
			}
			if (high == 0xdU) {
				plist_dict_set_item(n, (const char *)key->buf, val);
				plist_free(key);
			} else {
				plist_array_append_item(n, val);
			}
		}
		return n;
	}
	default:
		return node_new(K_NULL);
	}
}

void plist_from_bin(const char *plist_bin, uint32_t length, plist_t *plist)
{
	const uint8_t *p = (const uint8_t *)plist_bin;
	struct reader r = {.p = p, .len = length};
	uint64_t top, table;

	*plist = NULL;
	if (length < 8U + 32U || memcmp(p, "bplist00", 8) != 0) {
		return;
	}

	const uint8_t *t = p + length - 32;

	r.offset_size = t[6];
	r.ref_size = t[7];
	if (!be_read(t + 8, 8, &r.objects) || !be_read(t + 16, 8, &top) ||
	    !be_read(t + 24, 8, &table) || r.offset_size == 0U || r.offset_size > 8U ||
	    r.ref_size == 0U || r.ref_size > 8U || table > length ||
	    r.objects > (length - table) / r.offset_size) {
		return;
	}
	r.offset_table = table;
	*plist = read_object(&r, top, 0);
}

/* Binary writer */

struct writer {
	uint8_t *buf;
	uint32_t len;
	uint32_t cap;
	uint32_t *offsets;
	uint32_t count;
	uint8_t ref_size;
	bool fail;
};

static void put(struct writer *w, const void *data, uint32_t n)
{
	if (w->len + n > w->cap) {
		uint32_t cap = (w->cap != 0U ? w->cap : 256U);
		uint8_t *buf;

		while (cap < w->len + n) {
			cap *= 2U;
		}
		buf = realloc(w->buf, cap);
		if (buf == NULL) {
			w->fail = true;
			return;
		}
		w->buf = buf;
		w->cap = cap;
	}
	memcpy(w->buf + w->len, data, n);
	w->len += n;
}

static void put_be(struct writer *w, uint64_t v, unsigned int size)
{
	uint8_t b[8];

	for (unsigned int i = 0; i < size; i++) {
		b[size - 1 - i] = v >> (8 * i);
	}
	put(w, b, size);
}

static void put_head(struct writer *w, uint8_t high, uint64_t size)
{
	uint8_t m = high << 4;

	if (size < 15U) {
		m |= size;
		put(w, &m, 1);
	} else {
		m |= 0x0f;
		put(w, &m, 1);
		if (size < 256U) {
			put(w, "\x10", 1);
			put_be(w, size, 1);
		} else if (size < 65536U) {
			put(w, "\x11", 1);
			put_be(w, size, 2);
		} else {
			put(w, "\x12", 1);
			put_be(w, size, 4);
		}
	}
}

static uint32_t count_nodes(plist_t n)
{
	uint32_t c = 1;

	for (uint32_t i = 0; i < n->count; i++) {
		c += count_nodes(n->items[i]);
		if (n->kind == K_DICT) {
			c++;
		}
	}

	return c;
}

/* writes the node, its keys and children; returns the object number of the node */
static uint32_t write_node(struct writer *w, plist_t n)
{
	uint32_t id = w->count++;
	uint32_t keys_at = 0;

	w->offsets[id] = w->len;
	switch (n->kind) {
	case K_BOOL:
		put(w, n->num ? "\x09" : "\x08", 1);
		break;
	case K_UINT:
		if (n->num < 256U) {
			put(w, "\x10", 1);
			put_be(w, n->num, 1);
		} else if (n->num < 65536U) {
			put(w, "\x11", 1);
			put_be(w, n->num, 2);
		} else if (n->num <= 0xffffffffU) {
			put(w, "\x12", 1);
			put_be(w, n->num, 4);
		} else {
			put(w, "\x13", 1);
			put_be(w, n->num, 8);
		}
		break;
	case K_REAL: {
		uint64_t v;

		memcpy(&v, &n->real, sizeof(v));
		put(w, "\x23", 1);
		put_be(w, v, 8);
		break;
	}
	case K_STRING:
	case K_DATA:
		put_head(w, n->kind == K_STRING ? 0x5 : 0x4, n->len);
		put(w, n->buf, n->len);
		break;
	case K_ARRAY:
	case K_DICT: {
		uint32_t refs = n->kind == K_DICT ? n->count * 2 : n->count;

		put_head(w, n->kind == K_DICT ? 0xd : 0xa, n->count);
		keys_at = w->len;
		for (uint32_t i = 0; i < refs; i++) {
			put_be(w, 0, w->ref_size);
		}
		for (uint32_t i = 0; i < n->count; i++) {
			/* keys are strings of their own */
			if (n->kind == K_DICT) {
				uint32_t kid = w->count++;
				size_t kl = strlen(n->keys[i]);

				w->offsets[kid] = w->len;
				put_head(w, 0x5, kl);
				put(w, n->keys[i], kl);
				for (unsigned int b = 0; b < w->ref_size; b++) {
					w->buf[keys_at + i * w->ref_size + b] =
						kid >> (8 * (w->ref_size - 1 - b));
				}
			}
			uint32_t vid = write_node(w, n->items[i]);
			uint32_t at = keys_at + ((n->kind == K_DICT ? n->count : 0) + i) *
				      w->ref_size;

			for (unsigned int b = 0; b < w->ref_size; b++) {
				w->buf[at + b] = vid >> (8 * (w->ref_size - 1 - b));
			}
		}
		break;
	}
	default:
		put(w, "\x00", 1);
		break;
	}

	return id;
}

void plist_to_bin(plist_t plist, char **plist_bin, uint32_t *length)
{
	struct writer w = {0};
	uint32_t total = count_nodes(plist);
	uint32_t table;

	*plist_bin = NULL;
	*length = 0;
	w.ref_size = total < 256U ? 1 : 2;
	w.offsets = calloc(total, sizeof(*w.offsets));
	if (w.offsets == NULL) {
		return;
	}
	put(&w, "bplist00", 8);
	write_node(&w, plist);
	table = w.len;
	for (uint32_t i = 0; i < w.count; i++) {
		put_be(&w, w.offsets[i], 4);
	}
	put(&w, "\0\0\0\0\0\0", 6);
	put_be(&w, 4, 1);
	put_be(&w, w.ref_size, 1);
	put_be(&w, w.count, 8);
	put_be(&w, 0, 8);
	put_be(&w, table, 8);
	free(w.offsets);
	if (w.fail) {
		free(w.buf);
		return;
	}
	*plist_bin = (char *)w.buf;
	*length = w.len;
}

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Read only access to a JSON text that stays in memory: a value is a pointer to its first
 * character, nothing is copied until a string is asked for. The text must be valid JSON; every
 * function returns NULL (or -1) instead of walking past the terminating NUL of a cut-off one.
 */

#ifndef BILI_JSON_H_
#define BILI_JSON_H_

#include <stddef.h>

/** Past the value that starts at @p v; NULL for malformed text */
const char *js_skip(const char *v);

/** The value of @p key in the object @p obj, NULL when it is absent or @p obj is no object */
const char *js_get(const char *obj, const char *key);

/** Dotted path of keys: "data.owner.name" */
const char *js_path(const char *v, const char *path);

/** First and next element of an array (NULL at the end or for a non array) */
const char *js_first(const char *arr);
const char *js_next(const char *elem);

/** Decodes the string @p v (escapes, \uXXXX to UTF-8) into @p out; returns its length or -1 */
int js_str(const char *v, char *out, size_t size);

/** Integer value, @p def when @p v is NULL or not a number */
long long js_int(const char *v, long long def);

/** True when @p v is the string @p s (no escapes in @p s) */
int js_is(const char *v, const char *s);

#endif /* BILI_JSON_H_ */

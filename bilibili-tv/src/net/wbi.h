/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Request signing of the web API ("WBI") */

#ifndef BILI_WBI_H_
#define BILI_WBI_H_

#include <stddef.h>
#include <stdint.h>

/**
 * The 32 character signing key from the file names of the two key images of the nav response
 * (img_url / sub_url, any text that ends in "<key>.png" after the last slash).
 */
int wbi_make_key(const char *img_url, const char *sub_url, char key[33]);

/**
 * Builds the query string of a signed request. @p params is "name=value" pairs (values are
 * percent encoded here, characters the server drops are removed first); wts and w_rid are
 * added, the pairs are sorted by name. Returns the length or a negative errno.
 */
int wbi_query(const char *const *params, int count, const char *key, int64_t wts, char *out,
	      size_t size);

#endif /* BILI_WBI_H_ */

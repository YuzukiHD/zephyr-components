/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DLNA_SOAP_H_
#define DLNA_SOAP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Copies the text of the element @p name (any namespace prefix) from a SOAP body into @p out,
 * entities resolved (or literal text from a CDATA section). Capacity is checked after decoding.
 * False when the element is not there or the decoded value does not fit.
 */
bool soap_arg(const char *body, const char *name, char *out, size_t size);

/** The title of a DIDL-Lite document (the unescaped text of the first dc:title); "" if none */
void didl_title(const char *didl, char *out, size_t size);

/** Dimensions of the res element matching @p uri; false when absent or malformed. */
bool didl_resolution(const char *didl, const char *uri, uint32_t *width, uint32_t *height);

#endif /* DLNA_SOAP_H_ */

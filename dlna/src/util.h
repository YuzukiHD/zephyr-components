/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Text helpers of the protocol layer; no OS dependency so the host tests can build them */

#ifndef DLNA_UTIL_H_
#define DLNA_UTIL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Bounded text builder: never writes past the end, remembers an overflow */
struct sb {
	char *buf;
	size_t size, len;
	bool over;
};

void sb_init(struct sb *b, char *buf, size_t size);
void sb_puts(struct sb *b, const char *s);
void sb_printf(struct sb *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/** Appends @p s with the XML special characters replaced by entities */
void sb_puts_escaped(struct sb *b, const char *s);

/** Replaces the entities of @p s in place (&lt; &gt; &amp; &quot; &apos; &#N; &#xN;) */
void xml_unescape(char *s);

/** "H:MM:SS" (the UPnP time format, hours not limited) */
void fmt_time(uint32_t ms, char *out, size_t size);
/** Parses "H:MM:SS[.fff]" (also with a leading + and a fraction); false if malformed */
bool parse_time(const char *s, uint32_t *ms);

bool str_starts_ci(const char *s, const char *prefix);
/** Case insensitive search for a header line "Name:" in a block of lines; returns the value start */
const char *header_find(const char *headers, const char *name);

#endif /* DLNA_UTIL_H_ */

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "soap.h"
#include "util.h"

/* finds "<[prefix:]name" followed by '>' or whitespace or '/'; returns the char after the tag name */
static const char *find_open(const char *s, const char *name)
{
	size_t n = strlen(name);

	for (; (s = strchr(s, '<')) != NULL; s++) {
		const char *t = s + 1;
		const char *colon;

		if (*t == '/' || *t == '?' || *t == '!') {
			continue;
		}
		colon = t;
		while (*colon && *colon != ':' && *colon != '>' &&
		       !isspace((unsigned char)*colon) && *colon != '/') {
			colon++;
		}
		if (*colon == ':') {
			t = colon + 1;
		}
		if (!strncmp(t, name, n) &&
		    (t[n] == '>' || isspace((unsigned char)t[n]) || t[n] == '/')) {
			return t + n;
		}
	}
	return NULL;
}

bool soap_arg(const char *body, const char *name, char *out, size_t size)
{
	const char *p = find_open(body, name);
	const char *end;
	char *text;
	size_t n;
	bool cdata = false;

	if (!p || size == 0) {
		return false;
	}
	/* skip attributes */
	while (*p && *p != '>') {
		if (*p == '/' && p[1] == '>') {
			out[0] = '\0';	/* <Name/> is an empty value */
			return size > 0;
		}
		p++;
	}
	if (!*p) {
		return false;
	}
	p++;
	if (!strncmp(p, "<![CDATA[", 9)) {
		p += 9;
		end = strstr(p, "]]>");
		cdata = true;
	} else {
		end = strstr(p, "</");
	}
	if (!end) {
		return false;
	}
	n = end - p;
	text = malloc(n + 1);
	if (!text) {
		return false;
	}
	memcpy(text, p, n);
	text[n] = '\0';
	if (!cdata) {
		xml_unescape(text);
	}
	n = strlen(text);
	if (n >= size) {
		free(text);
		return false;
	}
	memcpy(out, text, n + 1);
	free(text);
	return true;
}

void didl_title(const char *didl, char *out, size_t size)
{
	const char *p = find_open(didl, "title");
	const char *end;
	size_t n;

	out[0] = '\0';
	if (!p) {
		return;
	}
	while (*p && *p != '>') {
		p++;
	}
	if (!*p) {
		return;
	}
	p++;
	end = strchr(p, '<');
	if (!end) {
		return;
	}
	n = end - p;
	if (n >= size) {
		n = size - 1;
		/* do not cut a UTF-8 sequence in the middle */
		while (n > 0 && ((unsigned char)p[n] & 0xc0) == 0x80) {
			n--;
		}
	}
	memcpy(out, p, n);
	out[n] = '\0';
	xml_unescape(out);
}

bool didl_resolution(const char *didl, const char *uri, uint32_t *width, uint32_t *height)
{
	const char *p = didl;
	char *resource = malloc(strlen(uri) + 1);
	bool found = false;

	if (!resource) {
		return false;
	}
	while ((p = find_open(p, "res")) != NULL) {
		const char *element = p;
		char resolution[32] = "";

		while (element > didl && *element != '<') {
			element--;
		}
		/* Walk quoted attributes so a URL or protocol value cannot masquerade as an attribute. */
		while (*p && *p != '>') {
			const char *name, *value;
			size_t name_len;
			char quote;

			while (isspace((unsigned char)*p)) {
				p++;
			}
			if (*p == '>' || *p == '/' || !*p) {
				break;
			}
			name = p;
			while (*p && *p != '=' && *p != '>' && !isspace((unsigned char)*p)) {
				p++;
			}
			name_len = p - name;
			while (isspace((unsigned char)*p)) {
				p++;
			}
			if (*p != '=') {
				break;
			}
			for (p++; isspace((unsigned char)*p); p++) {
			}
			quote = *p;
			if (quote != '\'' && quote != '"') {
				break;
			}
			value = ++p;
			while (*p && *p != quote) {
				p++;
			}
			if (!*p) {
				break;
			}
			if (name_len == 10 && !strncmp(name, "resolution", name_len) &&
			    (size_t)(p - value) < sizeof(resolution)) {
				memcpy(resolution, value, p - value);
				resolution[p - value] = '\0';
			}
			p++;
		}
		if (resolution[0] && soap_arg(element, "res", resource, strlen(uri) + 1) &&
		    !strcmp(resource, uri)) {
			char *end;
			unsigned long w, h;

			errno = 0;
			w = strtoul(resolution, &end, 10);
			if (!isdigit((unsigned char)resolution[0]) || *end != 'x' ||
			    !isdigit((unsigned char)end[1]) || errno == ERANGE || w == 0 || w > UINT32_MAX) {
				break;
			}
			h = strtoul(end + 1, &end, 10);
			if (*end || errno == ERANGE || h == 0 || h > UINT32_MAX) {
				break;
			}
			*width = w;
			*height = h;
			found = true;
			break;
		}
		if (!*p) {
			break;
		}
		p++;
	}
	free(resource);
	return found;
}

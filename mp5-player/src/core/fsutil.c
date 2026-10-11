/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/fs/fs.h>

#include "core/fsutil.h"

static char lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int name_cmp(const char *a, const char *b)
{
	while (*a != '\0' && lower(*a) == lower(*b)) {
		a++;
		b++;
	}
	return (unsigned char)lower(*a) - (unsigned char)lower(*b);
}

bool name_has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);

	return n > e && name_cmp(name + n - e, ext) == 0;
}

int file_read_text(const char *path, char *buf, size_t size)
{
	struct fs_file_t f;
	ssize_t n;
	int ret;

	fs_file_t_init(&f);
	ret = fs_open(&f, path, FS_O_READ);
	if (ret != 0) {
		return ret;
	}
	n = fs_read(&f, buf, size - 1);
	fs_close(&f);
	if (n < 0) {
		n = 0;
	}
	buf[n] = '\0';

	return n;
}

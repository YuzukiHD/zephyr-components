/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Small helpers for names of files */

#ifndef NEKOBOX_CORE_FSUTIL_H_
#define NEKOBOX_CORE_FSUTIL_H_

#include <stdbool.h>
#include <stddef.h>

/** Case blind comparison of file names */
int name_cmp(const char *a, const char *b);
/** True when @p name ends in @p ext (with the dot), whatever the case */
bool name_has_ext(const char *name, const char *ext);

/**
 * Reads the start of a small file into @p buf (up to size - 1 bytes) and ends it with a NUL.
 * Returns the number of bytes read, or a negative value when the file cannot be opened.
 */
int file_read_text(const char *path, char *buf, size_t size);

#endif /* NEKOBOX_CORE_FSUTIL_H_ */

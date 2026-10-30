/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
/* A reader intentionally keeps the size seen at open, as a FAT handle does. */
struct fs_file_t { int fd; uint64_t size; };
#define FS_O_READ 1
#define FS_O_WRITE 2
#define FS_O_CREATE 4
#define FS_SEEK_SET 0
void fs_file_t_init(struct fs_file_t *f);
int fs_open(struct fs_file_t *f, const char *path, int flags);
int fs_close(struct fs_file_t *f);
int fs_seek(struct fs_file_t *f, off_t off, int whence);
ssize_t fs_read(struct fs_file_t *f, void *buf, size_t len);
ssize_t fs_write(struct fs_file_t *f, const void *buf, size_t len);
int fs_sync(struct fs_file_t *f);
int fs_unlink(const char *path);

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_ZEPHYR_FS_H_
#define TEST_ZEPHYR_FS_H_

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define FS_O_READ O_RDONLY
#define FS_O_WRITE O_WRONLY
#define FS_O_CREATE O_CREAT
#define FS_O_TRUNC O_TRUNC

struct fs_file_t {
	int fd;
};

static inline void fs_file_t_init(struct fs_file_t *file)
{
	file->fd = -1;
}

static inline int fs_open(struct fs_file_t *file, const char *path, int flags)
{
	file->fd = open(path, flags, 0600);
	return file->fd < 0 ? -errno : 0;
}

static inline ssize_t fs_read(struct fs_file_t *file, void *buf, size_t size)
{
	ssize_t ret = read(file->fd, buf, size);

	return ret < 0 ? -errno : ret;
}

static inline ssize_t fs_write(struct fs_file_t *file, const void *buf, size_t size)
{
	ssize_t ret = write(file->fd, buf, size);

	return ret < 0 ? -errno : ret;
}

static inline int fs_close(struct fs_file_t *file)
{
	return close(file->fd) < 0 ? -errno : 0;
}

#endif

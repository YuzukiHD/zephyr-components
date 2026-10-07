/* SPDX-License-Identifier: Apache-2.0 */
/* POSIX file descriptor calls on top of the Zephyr file system (src/bochs_libc.c) */
#ifndef BOCHS_SHIM_UNISTD_H_
#define BOCHS_SHIM_UNISTD_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int open(const char *path, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
off_t lseek(int fd, off_t off, int whence);
int unlink(const char *path);
int ftruncate(int fd, off_t len);
int fsync(int fd);
int usleep(unsigned int us);
unsigned int sleep(unsigned int s);
int getpid(void);
#define F_OK 0
#define R_OK 4
#define W_OK 2
int access(const char *path, int mode);
int isatty(int fd);

#ifdef __cplusplus
}
#endif
#endif

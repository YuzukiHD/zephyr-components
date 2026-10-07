/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOCHS_SHIM_SYS_STAT_H_
#define BOCHS_SHIM_SYS_STAT_H_
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
struct stat {
	mode_t st_mode;
	off_t st_size;
	time_t st_mtime;
	time_t st_atime;
	time_t st_ctime;
	int st_blksize;
	long long st_blocks;
};
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISCHR(m) 0
#define S_ISBLK(m) 0
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IRGRP 040
#define S_IWGRP 020
#define S_IROTH 04
#define S_IWOTH 02
int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int mkdir(const char *path, mode_t mode);
#ifdef __cplusplus
}
#endif
#endif

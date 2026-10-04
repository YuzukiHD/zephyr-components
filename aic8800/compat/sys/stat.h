#ifndef AIC_COMPAT_SYS_STAT_H_
#define AIC_COMPAT_SYS_STAT_H_

#include <stdint.h>
#include <sys/types.h>

struct stat {
	uint32_t st_mode;
	off_t st_size;
};

#define S_IFMT	0170000
#define S_IFDIR	0040000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)

int stat(const char *path, struct stat *buf);

#endif

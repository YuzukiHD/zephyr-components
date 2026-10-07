/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * The C library functions Bochs needs that the minimal C library lacks:
 * file descriptors and file streams on the Zephyr file system, sscanf, time,
 * sleeping and a few string functions.
 */

#undef fopen
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/random/random.h>

/* undo the renaming of bochs_libc.h to reach the library's own functions */
#undef fclose
#undef fread
#undef fwrite
#undef fseek
#undef fseeko
#undef ftell
#undef ftello
#undef feof
#undef ferror
#undef fflush
#undef fputs
#undef fputc
#undef putc
#undef fgetc
#undef getc
#undef fgets
#undef vfprintf
#undef fprintf
#undef remove
#undef rename
#undef sscanf
#undef mktime

/* ---- file descriptors ------------------------------------------------- */

#define MAX_FDS 16

/* a file in memory ("mem:<name>", see bx_mem_file) */
struct mem_file {
	const char *name;
	const char *data;
	size_t len;
};

#define MAX_MEM_FILES 4
static struct mem_file mem_files[MAX_MEM_FILES];

void bx_mem_file(const char *name, const char *data, size_t len)
{
	for (int i = 0; i < MAX_MEM_FILES; i++) {
		if (mem_files[i].name == NULL) {
			mem_files[i].name = name;
			mem_files[i].data = data;
			mem_files[i].len = len;
			return;
		}
	}
}

static const struct mem_file *find_mem_file(const char *path)
{
	if (strncmp(path, "mem:", 4) != 0) {
		return NULL;
	}
	for (int i = 0; i < MAX_MEM_FILES; i++) {
		if (mem_files[i].name != NULL && strcmp(mem_files[i].name, path + 4) == 0) {
			return &mem_files[i];
		}
	}

	return NULL;
}

struct fd_slot {
	struct fs_file_t f;
	const struct mem_file *mem;
	size_t mem_pos;
	bool used;
};

static struct fd_slot fds[MAX_FDS];

int open(const char *path, int flags, ...)
{
	fs_mode_t mode;
	int fd;

	for (fd = 3; fd < MAX_FDS && fds[fd].used; fd++) {
	}
	if (fd == MAX_FDS) {
		errno = EMFILE;
		return -1;
	}
	fds[fd].mem = NULL;
	fds[fd].mem_pos = 0;
	if (strncmp(path, "mem:", 4) == 0) {
		fds[fd].mem = find_mem_file(path);
		if (fds[fd].mem == NULL) {
			errno = ENOENT;
			return -1;
		}
		fds[fd].used = true;
		return fd;
	}

	switch (flags & 3) {
	case 0:
		mode = FS_O_READ;
		break;
	case 1:
		mode = FS_O_WRITE;
		break;
	default:
		mode = FS_O_RDWR;
		break;
	}
	if (flags & 0x40) {
		mode |= FS_O_CREATE;
	}
	if (flags & 0x400) {
		mode |= FS_O_APPEND;
	}
	fs_file_t_init(&fds[fd].f);
	if (fs_open(&fds[fd].f, path, mode) != 0) {
		errno = ENOENT;
		return -1;
	}
	if (flags & 0x200) {
		fs_truncate(&fds[fd].f, 0);
	}
	fds[fd].used = true;

	return fd;
}

static bool fd_ok(int fd)
{
	return fd >= 3 && fd < MAX_FDS && fds[fd].used;
}

int close(int fd)
{
	if (!fd_ok(fd)) {
		errno = EBADF;
		return -1;
	}
	fds[fd].used = false;
	if (fds[fd].mem != NULL) {
		return 0;
	}

	return fs_close(&fds[fd].f) == 0 ? 0 : -1;
}

ssize_t read(int fd, void *buf, size_t n)
{
	if (!fd_ok(fd)) {
		errno = EBADF;
		return -1;
	}
	if (fds[fd].mem != NULL) {
		size_t left = fds[fd].mem->len - fds[fd].mem_pos;

		if (n > left) {
			n = left;
		}
		memcpy(buf, fds[fd].mem->data + fds[fd].mem_pos, n);
		fds[fd].mem_pos += n;

		return n;
	}

	/*
	 * The card controller reads by DMA and the cache is invalidated over the destination: a
	 * destination that does not fill whole cache lines would lose the dirty data next to it, so
	 * the data goes through a buffer of its own that is aligned to and a multiple of a line.
	 */
	{
		static uint8_t bounce[4096] __aligned(64);
		uint8_t *dst = buf;
		size_t total = 0;

		while (n > 0) {
			size_t chunk = n > sizeof(bounce) ? sizeof(bounce) : n;
			size_t rd = (chunk + 63) & ~(size_t)63;
			ssize_t got = fs_read(&fds[fd].f, bounce, rd);

			if (got < 0) {
				return total > 0 ? (ssize_t)total : got;
			}
			if ((size_t)got > chunk) {
				fs_seek(&fds[fd].f, (off_t)chunk - got, FS_SEEK_CUR);
				got = chunk;
			}
			memcpy(dst, bounce, got);
			dst += got;
			total += got;
			n -= got;
			if ((size_t)got < chunk) {
				break;
			}
		}

		return total;
	}
}

ssize_t write(int fd, const void *buf, size_t n)
{
	if (fd == 1 || fd == 2) {
		const char *p = buf;

		for (size_t i = 0; i < n; i++) {
			printk("%c", p[i]);
		}
		return n;
	}
	if (!fd_ok(fd) || fds[fd].mem != NULL) {
		errno = EBADF;
		return -1;
	}

	return fs_write(&fds[fd].f, buf, n);
}

off_t lseek(int fd, off_t off, int whence)
{
	if (!fd_ok(fd)) {
		errno = EBADF;
		return -1;
	}
	if (fds[fd].mem != NULL) {
		off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (off_t)fds[fd].mem_pos :
								(off_t)fds[fd].mem->len;

		if (base + off < 0) {
			return -1;
		}
		fds[fd].mem_pos = base + off;

		return base + off;
	}
	if (fs_seek(&fds[fd].f, off, whence == SEEK_SET ? FS_SEEK_SET :
				      whence == SEEK_CUR ? FS_SEEK_CUR : FS_SEEK_END) != 0) {
		return -1;
	}

	return fs_tell(&fds[fd].f);
}

int ftruncate(int fd, off_t len)
{
	return fd_ok(fd) && fds[fd].mem == NULL && fs_truncate(&fds[fd].f, len) == 0 ? 0 : -1;
}

int fsync(int fd)
{
	return fd_ok(fd) && fds[fd].mem == NULL && fs_sync(&fds[fd].f) == 0 ? 0 : -1;
}

int unlink(const char *path)
{
	return fs_unlink(path) == 0 ? 0 : -1;
}

int stat(const char *path, struct stat *st)
{
	struct fs_dirent ent;
	const struct mem_file *m = find_mem_file(path);

	if (m != NULL) {
		memset(st, 0, sizeof(*st));
		st->st_size = m->len;
		st->st_mode = S_IFREG;
		return 0;
	}

	if (fs_stat(path, &ent) != 0) {
		errno = ENOENT;
		return -1;
	}
	memset(st, 0, sizeof(*st));
	st->st_size = ent.size;
	st->st_mode = ent.type == FS_DIR_ENTRY_DIR ? S_IFDIR : S_IFREG;

	return 0;
}

int fstat(int fd, struct stat *st)
{
	off_t cur = lseek(fd, 0, SEEK_CUR);
	off_t end = lseek(fd, 0, SEEK_END);

	lseek(fd, cur, SEEK_SET);
	memset(st, 0, sizeof(*st));
	st->st_size = end;
	st->st_mode = S_IFREG;

	return 0;
}

int mkdir(const char *path, mode_t mode)
{
	return fs_mkdir(path) == 0 ? 0 : -1;
}

int isatty(int fd)
{
	return fd < 3;
}

int getpid(void)
{
	return 1;
}

/* ---- streams -------------------------------------------------------- */

struct bx_file {
	struct fs_file_t f;
	const char *mem; /* a file in memory (read only) */
	size_t mem_len, mem_pos;
	bool eof;
	bool err;
};

static bool is_std(FILE *f)
{
	return (uintptr_t)f <= 3U;
}

FILE *bx_fopen(const char *path, const char *mode)
{
	struct bx_file *d;
	fs_mode_t flags;

	d = calloc(1, sizeof(*d));
	if (d == NULL) {
		return NULL;
	}
	if (strncmp(path, "mem:", 4) == 0) {
		const struct mem_file *m = find_mem_file(path);

		if (m == NULL) {
			free(d);
			return NULL;
		}
		d->mem = m->data;
		d->mem_len = m->len;
		return (FILE *)d;
	}

	if (strchr(mode, 'w') != NULL) {
		flags = FS_O_WRITE | FS_O_CREATE | FS_O_TRUNC;
	} else if (strchr(mode, 'a') != NULL) {
		flags = FS_O_WRITE | FS_O_CREATE | FS_O_APPEND;
	} else {
		flags = FS_O_READ;
	}
	if (strchr(mode, '+') != NULL) {
		flags |= FS_O_RDWR;
	}
	fs_file_t_init(&d->f);
	if (fs_open(&d->f, path, flags) != 0) {
		free(d);
		return NULL;
	}

	return (FILE *)d;
}

int bx_fclose(FILE *f)
{
	struct bx_file *d = (struct bx_file *)f;
	int ret = 0;

	if (is_std(f)) {
		return 0;
	}
	if (d->mem == NULL) {
		ret = fs_close(&d->f);
	}
	free(d);

	return ret == 0 ? 0 : EOF;
}

size_t bx_fread(void *buf, size_t size, size_t n, FILE *f)
{
	struct bx_file *d = (struct bx_file *)f;
	ssize_t got;

	if (size == 0 || n == 0 || is_std(f)) {
		return 0;
	}
	if (d->mem != NULL) {
		size_t left = d->mem_len - d->mem_pos;
		size_t want = size * n;

		if (want > left) {
			want = left;
			d->eof = true;
		}
		memcpy(buf, d->mem + d->mem_pos, want);
		d->mem_pos += want;

		return want / size;
	}
	got = fs_read(&d->f, buf, size * n);
	if (got < 0) {
		d->err = true;
		return 0;
	}
	if ((size_t)got < size * n) {
		d->eof = true;
	}

	return got / size;
}

size_t bx_fwrite(const void *buf, size_t size, size_t n, FILE *f)
{
	struct bx_file *d;
	ssize_t put;

	if (is_std(f)) {
		const char *p = buf;

		for (size_t i = 0; i < size * n; i++) {
			printk("%c", p[i]);
		}
		return n;
	}
	d = (struct bx_file *)f;
	if (size == 0 || n == 0 || d->mem != NULL) {
		return 0;
	}
	put = fs_write(&d->f, buf, size * n);
	if (put < 0) {
		d->err = true;
		return 0;
	}

	return put / size;
}

int bx_fseek(FILE *f, long off, int whence)
{
	struct bx_file *d = (struct bx_file *)f;

	d->eof = false;
	if (d->mem != NULL) {
		long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)d->mem_pos :
							    (long)d->mem_len;
		long p = base + off;

		if (p < 0 || p > (long)d->mem_len) {
			return -1;
		}
		d->mem_pos = p;
		return 0;
	}

	return fs_seek(&d->f, off, whence == SEEK_SET ? FS_SEEK_SET :
				   whence == SEEK_CUR ? FS_SEEK_CUR : FS_SEEK_END) == 0 ? 0 : -1;
}

long bx_ftell(FILE *f)
{
	struct bx_file *d = (struct bx_file *)f;

	return d->mem != NULL ? (long)d->mem_pos : fs_tell(&d->f);
}

int bx_feof(FILE *f)
{
	return ((struct bx_file *)f)->eof;
}

int bx_ferror(FILE *f)
{
	return is_std(f) ? 0 : ((struct bx_file *)f)->err;
}

int bx_fflush(FILE *f)
{
	struct bx_file *d = (struct bx_file *)f;

	if (f == NULL || is_std(f) || d->mem != NULL) {
		return 0;
	}

	return fs_sync(&d->f) == 0 ? 0 : EOF;
}

int bx_fputc(int c, FILE *f)
{
	unsigned char ch = c;

	return bx_fwrite(&ch, 1, 1, f) == 1 ? c : EOF;
}

int bx_fgetc(FILE *f)
{
	unsigned char ch;

	return bx_fread(&ch, 1, 1, f) == 1 ? ch : EOF;
}

char *bx_fgets(char *s, int n, FILE *f)
{
	int i = 0;

	while (i < n - 1) {
		int c = bx_fgetc(f);

		if (c == EOF) {
			break;
		}
		s[i++] = c;
		if (c == '\n') {
			break;
		}
	}
	s[i] = '\0';

	return i > 0 ? s : NULL;
}

int bx_fputs(const char *s, FILE *f)
{
	size_t n = strlen(s);

	return bx_fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int bx_vfprintf(FILE *f, const char *fmt, va_list ap)
{
	char buf[512];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);

	if (n > (int)sizeof(buf) - 1) {
		n = sizeof(buf) - 1;
	}
	if (n > 0) {
		bx_fwrite(buf, 1, n, f);
	}

	return n;
}

int bx_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = bx_vfprintf(f, fmt, ap);
	va_end(ap);

	return n;
}

int bx_remove(const char *path)
{
	return fs_unlink(path);
}

int bx_rename(const char *from, const char *to)
{
	return fs_rename(from, to);
}

/*
 * sscanf: %d %i %u %x %o (with h/hh/l/ll/z), %s, %c, %[set], %n, %%, a width,
 * assignment suppression and literal text
 */
int bx_sscanf(const char *str, const char *fmt, ...)
{
	va_list ap;
	int count = 0;
	const char *start = str;

	va_start(ap, fmt);
	while (*fmt != '\0') {
		if (isspace((unsigned char)*fmt)) {
			while (isspace((unsigned char)*str)) {
				str++;
			}
			fmt++;
			continue;
		}
		if (*fmt != '%') {
			if (*str != *fmt) {
				break;
			}
			str++;
			fmt++;
			continue;
		}
		fmt++;
		if (*fmt == '%') {
			if (*str != '%') {
				break;
			}
			str++;
			fmt++;
			continue;
		}

		bool suppress = false;
		int width = 0, lmod = 0;

		if (*fmt == '*') {
			suppress = true;
			fmt++;
		}
		while (isdigit((unsigned char)*fmt)) {
			width = width * 10 + (*fmt++ - '0');
		}
		while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
			lmod += *fmt == 'h' ? -1 : 1;
			fmt++;
		}
		char conv = *fmt++;

		if (conv == 'n') {
			if (!suppress) {
				*va_arg(ap, int *) = str - start;
			}
			continue;
		}
		if (conv == 'c') {
			if (*str == '\0') {
				break;
			}
			if (!suppress) {
				*va_arg(ap, char *) = *str;
				count++;
			}
			str++;
			continue;
		}
		if (conv == '[') {
			bool neg = false;
			char set[256] = {0};

			if (*fmt == '^') {
				neg = true;
				fmt++;
			}
			if (*fmt == ']') {
				set[(unsigned char)*fmt++] = 1;
			}
			while (*fmt != '\0' && *fmt != ']') {
				if (fmt[1] == '-' && fmt[2] != ']' && fmt[2] != '\0') {
					for (int c = (unsigned char)fmt[0]; c <= (unsigned char)fmt[2]; c++) {
						set[c] = 1;
					}
					fmt += 3;
				} else {
					set[(unsigned char)*fmt++] = 1;
				}
			}
			if (*fmt == ']') {
				fmt++;
			}
			char *out = suppress ? NULL : va_arg(ap, char *);
			int n = 0;

			while (*str != '\0' && (set[(unsigned char)*str] != 0) != neg &&
			       (width == 0 || n < width)) {
				if (out != NULL) {
					out[n] = *str;
				}
				n++;
				str++;
			}
			if (n == 0) {
				break;
			}
			if (out != NULL) {
				out[n] = '\0';
				count++;
			}
			continue;
		}

		while (isspace((unsigned char)*str)) {
			str++;
		}
		if (*str == '\0') {
			if (count == 0) {
				va_end(ap);
				return EOF;
			}
			break;
		}
		if (conv == 's') {
			char *out = suppress ? NULL : va_arg(ap, char *);
			int n = 0;

			while (*str != '\0' && !isspace((unsigned char)*str) &&
			       (width == 0 || n < width)) {
				if (out != NULL) {
					out[n] = *str;
				}
				n++;
				str++;
			}
			if (out != NULL) {
				out[n] = '\0';
				count++;
			}
			continue;
		}
		if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' ||
		    conv == 'o') {
			int base = conv == 'd' || conv == 'u' ? 10 : conv == 'o' ? 8 :
				   conv == 'i' ? 0 : 16;
			char *end;
			char tmp[40];
			const char *src = str;

			if (width > 0 && width < (int)sizeof(tmp)) {
				strncpy(tmp, str, width);
				tmp[width] = '\0';
				src = tmp;
			}
			unsigned long long v;

			if (conv == 'd' || conv == 'i') {
				v = (unsigned long long)strtoll(src, &end, base);
			} else {
				v = strtoull(src, &end, base);
			}
			if (end == src) {
				break;
			}
			str += end - src;
			if (!suppress) {
				void *p = va_arg(ap, void *);

				if (lmod >= 2) {
					*(unsigned long long *)p = v;
				} else if (lmod == 1) {
					*(unsigned long *)p = v;
				} else if (lmod == -1) {
					*(unsigned short *)p = v;
				} else if (lmod <= -2) {
					*(unsigned char *)p = v;
				} else {
					*(unsigned int *)p = v;
				}
				count++;
			}
			continue;
		}
		if (conv == 'f' || conv == 'g' || conv == 'e') {
			char *end;
			double v = strtod(str, &end);

			if (end == str) {
				break;
			}
			str = end;
			if (!suppress) {
				if (lmod >= 1) {
					*va_arg(ap, double *) = v;
				} else {
					*va_arg(ap, float *) = v;
				}
				count++;
			}
			continue;
		}
		break;
	}
	va_end(ap);

	return count;
}

/* ---- strings and the environment ------------------------------------ */

char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);

	if (p != NULL) {
		memcpy(p, s, n);
	}

	return p;
}

int strcasecmp(const char *a, const char *b)
{
	return strncasecmp(a, b, SIZE_MAX);
}

char *getenv(const char *name)
{
	return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
	return 0;
}

char *setlocale(int category, const char *locale)
{
	return (char *)"C";
}

int system(const char *cmd)
{
	return -1;
}

static uint32_t rand_state = 1;

void srand(unsigned int seed)
{
	rand_state = seed;
}

int rand(void)
{
	rand_state = rand_state * 1103515245U + 12345U;

	return (rand_state >> 16) & 0x7fff;
}

/* ---- time ----------------------------------------------------------- */

int gettimeofday(struct timeval *tv, void *tz)
{
	int64_t us = k_ticks_to_us_floor64(k_uptime_ticks());

	tv->tv_sec = us / 1000000;
	tv->tv_usec = us % 1000000;

	return 0;
}

int usleep(unsigned int us)
{
	k_usleep(us);

	return 0;
}

unsigned int sleep(unsigned int s)
{
	k_sleep(K_SECONDS(s));

	return 0;
}

char *strtok(char *s, const char *delim)
{
	static char *save;
	char *tok;

	if (s == NULL) {
		s = save;
	}
	if (s == NULL) {
		return NULL;
	}
	s += strspn(s, delim);
	if (*s == '\0') {
		save = NULL;
		return NULL;
	}
	tok = s;
	s += strcspn(s, delim);
	if (*s != '\0') {
		*s++ = '\0';
	}
	save = s;

	return tok;
}

char *mktemp(char *tpl)
{
	return tpl;
}

FILE *fdopen(int fd, const char *mode)
{
	return NULL;
}

long atol(const char *s)
{
	return strtol(s, NULL, 10);
}

/* the broken down time is taken as UTC: the board has no time zone */
long long bx_mktime(struct tm *tm)
{
	long long y = tm->tm_year + 1900LL, m = tm->tm_mon + 1;
	long long days;

	if (m <= 2) {
		y--;
		m += 12;
	}
	days = 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + tm->tm_mday - 719469;

	return ((days * 24 + tm->tm_hour) * 60 + tm->tm_min) * 60 + tm->tm_sec;
}

FILE *tmpfile(void)
{
	return NULL;
}

/* sign, digits, fraction and exponent, no hex or inf/nan */
double strtod(const char *s, char **end)
{
	const char *p = s;
	double v = 0.0, scale = 1.0;
	bool neg = false, any = false;
	int e = 0;

	while (isspace((unsigned char)*p)) {
		p++;
	}
	if (*p == '-' || *p == '+') {
		neg = *p++ == '-';
	}
	while (isdigit((unsigned char)*p)) {
		v = v * 10.0 + (*p++ - '0');
		any = true;
	}
	if (*p == '.') {
		p++;
		while (isdigit((unsigned char)*p)) {
			scale /= 10.0;
			v += (*p++ - '0') * scale;
			any = true;
		}
	}
	if (!any) {
		if (end != NULL) {
			*end = (char *)s;
		}
		return 0.0;
	}
	if (*p == 'e' || *p == 'E') {
		const char *q = p + 1;
		bool eneg = false;

		if (*q == '-' || *q == '+') {
			eneg = *q++ == '-';
		}
		if (isdigit((unsigned char)*q)) {
			while (isdigit((unsigned char)*q)) {
				e = e * 10 + (*q++ - '0');
			}
			p = q;
			while (e-- > 0) {
				v = eneg ? v / 10.0 : v * 10.0;
			}
		}
	}
	if (end != NULL) {
		*end = (char *)p;
	}

	return neg ? -v : v;
}

int access(const char *path, int mode)
{
	struct stat st;

	return stat(path, &st);
}

/* the math functions of the toolchain report errors through this */
int *__errno(void)
{
	return &errno;
}

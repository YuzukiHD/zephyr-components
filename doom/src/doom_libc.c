/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#undef fopen
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* undo the renaming of doom_libc.h to reach the library's own functions */
#undef fclose
#undef fread
#undef fwrite
#undef fseek
#undef ftell
#undef feof
#undef fflush
#undef fputs
#undef fputc
#undef putc
#undef vfprintf
#undef fprintf
#undef remove
#undef rename
#undef sscanf
#undef mkdir

/* relative paths ("./default.cfg", ".savegame/") are taken below this directory */
char doom_base_dir[64] = "/SD:";

static const char *resolve(const char *path, char *out, size_t size)
{
	while (path[0] == '.' && path[1] == '/') {
		path += 2;
	}
	if (path[0] == '/') {
		return path;
	}
	snprintf(out, size, "%s/%s", doom_base_dir, path);

	return out;
}

struct doom_file {
	struct fs_file_t f;
	bool eof;
	bool err;
};

/* stdout, stderr and stdin are the library's own pseudo streams (1, 2, 3) */
static bool is_std(FILE *f)
{
	return (uintptr_t)f <= 3U;
}

FILE *doom_fopen(const char *path, const char *mode)
{
	struct doom_file *d;
	char full[160];
	fs_mode_t flags;

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
	d = calloc(1, sizeof(*d));
	if (d == NULL) {
		return NULL;
	}
	fs_file_t_init(&d->f);
	if (fs_open(&d->f, resolve(path, full, sizeof(full)), flags) != 0) {
		free(d);
		return NULL;
	}

	return (FILE *)d;
}

int doom_fclose(FILE *f)
{
	struct doom_file *d = (struct doom_file *)f;
	int ret = fs_close(&d->f);

	free(d);

	return ret == 0 ? 0 : EOF;
}

size_t doom_fread(void *buf, size_t size, size_t n, FILE *f)
{
	struct doom_file *d = (struct doom_file *)f;
	ssize_t got;

	if (size == 0 || n == 0) {
		return 0;
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

size_t doom_fwrite(const void *buf, size_t size, size_t n, FILE *f)
{
	struct doom_file *d;
	ssize_t put;

	if (is_std(f)) {
		const char *p = buf;

		for (size_t i = 0; i < size * n; i++) {
			printk("%c", p[i]);
		}
		return n;
	}
	d = (struct doom_file *)f;
	if (size == 0 || n == 0) {
		return 0;
	}
	put = fs_write(&d->f, buf, size * n);
	if (put < 0) {
		d->err = true;
		return 0;
	}

	return put / size;
}

int doom_fseek(FILE *f, long off, int whence)
{
	struct doom_file *d = (struct doom_file *)f;

	d->eof = false;

	return fs_seek(&d->f, off, whence == SEEK_SET ? FS_SEEK_SET :
				   whence == SEEK_CUR ? FS_SEEK_CUR : FS_SEEK_END) == 0 ? 0 : -1;
}

long doom_ftell(FILE *f)
{
	return fs_tell(&((struct doom_file *)f)->f);
}

int doom_feof(FILE *f)
{
	return ((struct doom_file *)f)->eof;
}

int doom_fflush(FILE *f)
{
	if (is_std(f)) {
		return 0;
	}

	return fs_sync(&((struct doom_file *)f)->f) == 0 ? 0 : EOF;
}

int doom_fputc(int c, FILE *f)
{
	unsigned char ch = c;

	return doom_fwrite(&ch, 1, 1, f) == 1 ? c : EOF;
}

int doom_fputs(const char *s, FILE *f)
{
	size_t n = strlen(s);

	return doom_fwrite(s, 1, n, f) == n ? 0 : EOF;
}

int doom_vfprintf(FILE *f, const char *fmt, va_list ap)
{
	char buf[256];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);

	if (n > (int)sizeof(buf) - 1) {
		n = sizeof(buf) - 1;
	}
	if (n > 0) {
		doom_fwrite(buf, 1, n, f);
	}

	return n;
}

int doom_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = doom_vfprintf(f, fmt, ap);
	va_end(ap);

	return n;
}

int doom_remove(const char *path)
{
	char full[160];

	return fs_unlink(resolve(path, full, sizeof(full)));
}

int doom_rename(const char *from, const char *to)
{
	char a[160], b[160];

	return fs_rename(resolve(from, a, sizeof(a)), resolve(to, b, sizeof(b)));
}

int doom_mkdir(const char *path, int mode)
{
	char full[160];
	size_t len;
	int ret;

	/* "." is the base directory, a trailing slash is not part of the name */
	resolve(strcmp(path, ".") == 0 ? "" : path, full, sizeof(full));
	len = strlen(full);
	while (len > 1U && full[len - 1U] == '/') {
		full[--len] = '\0';
	}

	/* the base directory itself is the mount point */
	if (strcmp(full, doom_base_dir) == 0) {
		return 0;
	}

	/* a directory that is there already is what the caller wants; the file system layer logs
	 * an error for every mkdir of an existing one, so look first
	 */
	struct fs_dirent st;

	if (fs_stat(full, &st) == 0 && st.type == FS_DIR_ENTRY_DIR) {
		return 0;
	}
	ret = fs_mkdir(full);

	return ret == -EEXIST ? 0 : ret;
}

/* %d %i %x %o and literal text: the formats of the number parsing in the config code */
int doom_sscanf(const char *str, const char *fmt, ...)
{
	va_list ap;
	int count = 0;

	va_start(ap, fmt);
	while (*fmt != '\0') {
		if (isspace((unsigned char)*fmt)) {
			while (isspace((unsigned char)*str)) {
				str++;
			}
			fmt++;
		} else if (*fmt == '%') {
			int base = 10;
			char *end;
			long v;

			fmt++;
			switch (*fmt++) {
			case 'd':
				base = 10;
				break;
			case 'i':
				base = 0;
				break;
			case 'x':
				base = 16;
				break;
			case 'o':
				base = 8;
				break;
			default:
				va_end(ap);
				return count;
			}
			while (isspace((unsigned char)*str)) {
				str++;
			}
			v = strtol(str, &end, base);
			if (end == str) {
				va_end(ap);
				return count;
			}
			*va_arg(ap, int *) = v;
			count++;
			str = end;
		} else {
			if (*str != *fmt) {
				break;
			}
			str++;
			fmt++;
		}
	}
	va_end(ap);

	return count;
}

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

int system(const char *cmd)
{
	return -1;
}

/* sign, digits, fraction; no exponent: the values of the config file */
double atof(const char *s)
{
	double v = 0.0, scale = 1.0;
	bool neg = false;

	while (isspace((unsigned char)*s)) {
		s++;
	}
	if (*s == '-' || *s == '+') {
		neg = *s++ == '-';
	}
	while (isdigit((unsigned char)*s)) {
		v = v * 10.0 + (*s++ - '0');
	}
	if (*s == '.') {
		s++;
		while (isdigit((unsigned char)*s)) {
			scale /= 10.0;
			v += (*s++ - '0') * scale;
		}
	}

	return neg ? -v : v;
}

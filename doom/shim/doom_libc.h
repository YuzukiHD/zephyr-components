/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Force-included into every Doom source. The minimal C library has no file
 * streams, sscanf, strdup or the case insensitive string compares the engine
 * uses, src/doom_libc.c has them on top of the Zephyr file system; the stream
 * functions are renamed so they cannot clash with the library's own
 * stdout/stderr versions (which they pass through to).
 */

#ifndef DOOM_LIBC_H_
#define DOOM_LIBC_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE *doom_fopen(const char *path, const char *mode);
int doom_fclose(FILE *f);
size_t doom_fread(void *buf, size_t size, size_t n, FILE *f);
size_t doom_fwrite(const void *buf, size_t size, size_t n, FILE *f);
int doom_fseek(FILE *f, long off, int whence);
long doom_ftell(FILE *f);
int doom_feof(FILE *f);
int doom_fflush(FILE *f);
int doom_fputs(const char *s, FILE *f);
int doom_fputc(int c, FILE *f);
int doom_vfprintf(FILE *f, const char *fmt, va_list ap);
int doom_fprintf(FILE *f, const char *fmt, ...);
int doom_remove(const char *path);
int doom_rename(const char *from, const char *to);
int doom_sscanf(const char *str, const char *fmt, ...);
int doom_mkdir(const char *path, int mode);

#define fopen doom_fopen
#define fclose doom_fclose
#define fread doom_fread
#define fwrite doom_fwrite
#define fseek doom_fseek
#define ftell doom_ftell
#define feof doom_feof
#define fflush doom_fflush
#define fputs doom_fputs
#define fputc doom_fputc
#define putc(c, f) doom_fputc(c, f)
#define vfprintf doom_vfprintf
#define fprintf doom_fprintf
#define remove doom_remove
#define rename doom_rename
#define sscanf doom_sscanf
#define mkdir doom_mkdir

char *strdup(const char *s);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *getenv(const char *name);
int system(const char *cmd);
double atof(const char *s);

/* the minimal C library has no fabs */
#define fabs(x) __builtin_fabs(x)

#endif

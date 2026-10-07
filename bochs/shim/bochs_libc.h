/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Force-included into every Bochs source. The minimal C library of the image has
 * no file streams, sscanf, or file descriptors: src/bochs_libc.c has them on top of
 * the Zephyr file system. The stream functions are renamed so they cannot clash
 * with the library's own stdout/stderr versions (which they pass through to).
 */
#ifndef BOCHS_LIBC_H_
#define BOCHS_LIBC_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "mem:<name>" opens an embedded file registered with bx_mem_file() */
void bx_mem_file(const char *name, const char *data, size_t len);

FILE *bx_fopen(const char *path, const char *mode);
int bx_fclose(FILE *f);
size_t bx_fread(void *buf, size_t size, size_t n, FILE *f);
size_t bx_fwrite(const void *buf, size_t size, size_t n, FILE *f);
int bx_fseek(FILE *f, long off, int whence);
long bx_ftell(FILE *f);
int bx_feof(FILE *f);
int bx_ferror(FILE *f);
int bx_fflush(FILE *f);
int bx_fputs(const char *s, FILE *f);
int bx_fputc(int c, FILE *f);
int bx_fgetc(FILE *f);
char *bx_fgets(char *s, int n, FILE *f);
int bx_vfprintf(FILE *f, const char *fmt, va_list ap);
int bx_fprintf(FILE *f, const char *fmt, ...);
int bx_remove(const char *path);
int bx_rename(const char *from, const char *to);
int bx_sscanf(const char *str, const char *fmt, ...);

void srand(unsigned int seed);
int rand(void);
char *strdup(const char *s);
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
char *setlocale(int category, const char *locale);
void abort(void);
char *strtok(char *s, const char *delim);
char *mktemp(char *tpl);
FILE *fdopen(int fd, const char *mode);
#include <time.h>
/* time_t is 32 bit here, Bochs needs the years 0000..9999 */
long long bx_mktime(struct tm *tm);
#define mktime bx_mktime
long atol(const char *s);
double strtod(const char *s, char **end);
FILE *tmpfile(void);
long long strtoll(const char *s, char **end, int base);

#ifdef __cplusplus
}
#endif

#define LC_ALL 0
#define LC_NUMERIC 1

#define fopen bx_fopen
#define fclose bx_fclose
#define fread bx_fread
#define fwrite bx_fwrite
#define fseek bx_fseek
#define fseeko bx_fseek
#define ftell bx_ftell
#define ftello bx_ftell
#define feof bx_feof
#define ferror bx_ferror
#define fflush bx_fflush
#define fputs bx_fputs
#define fputc bx_fputc
#define putc(c, f) bx_fputc(c, f)
#define fgetc bx_fgetc
#define getc bx_fgetc
#define fgets bx_fgets
#define vfprintf bx_vfprintf
#define fprintf bx_fprintf
#define remove bx_remove
#define rename bx_rename
#define sscanf bx_sscanf

#endif

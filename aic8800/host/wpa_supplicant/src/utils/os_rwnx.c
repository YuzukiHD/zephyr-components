/*
 * OS specific functions for RWNX system
 * Copyright (C) RivieraWaves 2017-2019
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "os.h"
#include "trace.h"
//#include "time_api.h"
#include <stdarg.h>
#include "co_math.h"

#define TRACE 	aic_dbg
#if 1
void os_sleep(os_time_t sec, os_time_t usec)
{
	uint32_t delay;
	delay = (sec * 1000) + (usec / 1000);
	rtos_task_suspend(delay);
}

int os_get_time(struct os_time *t)
{
	uint32_t sec, usec;
	int ret;

	ret = aic_time_get(0, &sec, &usec);
	t->sec = sec;
	t->usec = usec;
	return ret;
}

int os_get_reltime(struct os_reltime *t)
{
	uint32_t sec, usec;
	int ret = 0;
	if (!t)
		return -1;
	ret = aic_time_get(0, &sec, &usec);
	t->sec = sec;
	t->usec = usec;
	return ret;
}

int os_mktime(int year, int month, int day, int hour, int min, int sec,
	      os_time_t *t)
{
	TRACE("TODO: os_mktime");
	return -1;
}

int os_gmtime(os_time_t t, struct os_tm *tm)
{
	TRACE("TODO: os_gmtime");
	return -1;
}
#endif

int os_daemonize(const char *pid_file)
{
	TRACE("TODO: os_daemonize");
	return -1;
}


void os_daemonize_terminate(const char *pid_file)
{
	TRACE("TODO: os_daemonize_terminate");
}
#if 1

/* TODO: Check this is good enough */
int os_get_random(unsigned char *buf, size_t len)
{
	size_t i;
	for (i = 0; i < len ; i++) {
		#if 1
		buf[i] = co_rand_byte();
		#else
		buf[i] = (unsigned char)trng_get_word();
		#endif
	}
	return 0;
}

unsigned long os_random(void)
{
	return co_rand_word();
}
#endif


char * os_rel2abs_path(const char *rel_path)
{
	TRACE("TODO: os_rel2abs_path");
	return NULL; /* strdup(rel_path) can be used here */
}


int os_program_init(void)
{
	TRACE("TODO: os_program_init");
	return 0;
}


void os_program_deinit(void)
{
	TRACE("TODO: os_program_deinit");
}


int os_setenv(const char *name, const char *value, int overwrite)
{
	TRACE("TODO: os_setenv");
	return -1;
}

int os_unsetenv(const char *name)
{
	TRACE("TODO: os_unsetenv");
	return -1;
}


char * os_readfile(const char *name, size_t *len)
{
	TRACE("TODO: os_readfile");
	return NULL;
}


int os_fdatasync(FILE *stream)
{
	TRACE("TODO: os_fdatasync");
	return 0;
}

#if 1
void * os_zalloc(size_t size)
{
	void * ptr = rtos_malloc(size);
	if (ptr) {
		os_memset(ptr, 0, size);
	}
	return ptr;
}

#endif
#ifdef OS_NO_C_LIB_DEFINES
void * os_malloc(size_t size)
{
	return rtos_malloc(size);
}
#if 1
void * os_memdup(const void *src, size_t len)
{
    void *r = os_malloc(len);

    if (r)
        os_memcpy(r, src, len);
    return r;
}
#endif
void * os_realloc(void *ptr, size_t size)
{
	void *res;

	if (!ptr)
		return os_malloc(size);

	if (!size) {
		os_free(ptr);
		return NULL;
	}

	res = os_malloc(size);
	if(res) {
		if (size)
			os_memcpy(res, ptr, size);
		os_free(ptr);
	}

	return res;
}


void os_free(void *ptr)
{
	if (ptr == NULL)
		return;
	rtos_free(ptr);
}


void * os_memcpy_1(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}


void * os_memmove(void *dest, const void *src, size_t n)
{
	return memmove(dest, src, n);
}


void * os_memset(void *s, int c, size_t n)
{
	return memset(s, c, n);
}


int os_memcmp(const void *s1, const void *s2, size_t n)
{
	const uint8_t *ptr1=s1, *ptr2=s2;

	while(n) {
		int diff = (*ptr1++ - *ptr2++);
		if (diff)
			return diff;
		n--;
	}

	return 0;
}


char * os_strdup(const char *s)
{
	char *res;
	size_t len = os_strlen(s) + 1;
	res = os_malloc(len);
	if (res)
		os_memcpy(res, s, len);

	return res;
}


#undef os_strlen
size_t os_strlen(const char *s)
{
	return strlen(s);
}


int os_strcasecmp(const char *s1, const char *s2)
{
	/*
	 * Ignoring case is not required for main functionality, so just use
	 * the case sensitive version of the function.
	 */
	return os_strcmp(s1, s2);
}


int os_strncasecmp(const char *s1, const char *s2, size_t n)
{
	/*
	 * Ignoring case is not required for main functionality, so just use
	 * the case sensitive version of the function.
	 */
	return os_strncmp(s1, s2, n);
}


char * os_strchr(const char *s, int c)
{
	return strchr(s, c);
}


char * os_strrchr(const char *s, int c)
{
	return strrchr(s, c);
}


int os_strcmp(const char *s1, const char *s2)
{
	return strncmp(s1, s2, strlen(s2));
}


int os_strncmp(const char *s1, const char *s2, size_t n)
{
	return strncmp(s1, s2, n);
}


char * os_strncpy(char *dest, const char *src, size_t n)
{
	return strncpy(dest, src, n);
}

#if 1
size_t os_strlcpy(char *dest, const char *src, size_t size)
{
	const char *s = src;
	size_t left = size;

	if (left) {
		/* Copy string up to the maximum size of the dest buffer */
		while (--left != 0) {
			if ((*dest++ = *s++) == '\0')
				break;
		}
	}

	if (left == 0) {
		/* Not enough room for the string; force NUL-termination */
		if (size != 0)
			*dest = '\0';
		while (*s++)
			; /* determine total src string length */
	}

	return s - src - 1;
}

int os_memcmp_const(const void *a, const void *b, size_t len)
{
	const uint8_t *aa = a;
	const uint8_t *bb = b;
	size_t i;
	uint8_t res;

	for (res = 0, i = 0; i < len; i++)
		res |= aa[i] ^ bb[i];

	return res;
}
#endif
char * os_strstr(const char *haystack, const char *needle)
{
	return strstr(haystack, needle);
}



int os_snprintf(char *str, size_t size, const char *format, ...)
{
	int ret;
	va_list args;
	va_start(args, format);
	ret = dbg_vsnprintf(str, size, format, args);
	va_end(args);

	return ret;
}
#endif /* OS_NO_C_LIB_DEFINES */


int os_exec(const char *program, const char *arg, int wait_completion)
{
	TRACE("TODO: os_exec");
	return -1;
}

/*
 * Stub implementations for symbols that may not get macro-expanded
 * via os.h in certain compilation contexts (e.g., when used as
 * function pointers or via IPA-SRA compiler clones on RISC-V).
 * The RISC-V Xuantie V3.3.0 toolchain with -Os may clone functions
 * and emit direct calls to os_memcpy/TEST_FAIL as external symbols
 * instead of inlining the macro, causing undefined reference at link time.
 */
#ifdef os_memcpy
#undef os_memcpy
#endif
void *os_memcpy(void *dest, const void *src, size_t n)
{
	return memcpy(dest, src, n);
}

/*
 * TEST_FAIL() is normally a macro expanding to 0 (os.h:683).
 * RISC-V IPA-SRA may emit a call to TEST_FAIL as an external function.
 * This stub must return 0 (not fail) to match the macro semantics.
 * The previous version incorrectly defined it as void TEST_FAIL(void),
 * which caused sha1_vector/sha256_vector to return -1 (failure) and
 * broke the 4-way handshake.
 */
#ifdef TEST_FAIL
#undef TEST_FAIL
#endif
int TEST_FAIL(void)
{
	return 0;
}


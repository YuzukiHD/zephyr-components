/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * What the supplicant takes from the C library and the socket layer besides the
 * socket calls of lwip/sockets.h: file access (there is none under the supplicant), a
 * few string and math functions of the minimal C library's gaps. src/aic_libc.c
 * has them.
 */

#ifndef AIC_COMPAT_SOCK_H_
#define AIC_COMPAT_SOCK_H_

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO	0
#endif

int getchar(void);
int rename(const char *from, const char *to);
char *fgets(char *s, int n, FILE *f);
double strtod(const char *s, char **end);
double pow(double x, double y);
int sscanf(const char *str, const char *fmt, ...);

/* no file system under the supplicant: opening and removing files fails */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
int unlink(const char *path);

#endif

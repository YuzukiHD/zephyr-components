/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Force-included into every PocketJS and QuickJS source: what the minimal C library
 * lacks, implemented in src/pocketjs_std.c.
 */
#pragma once

#include <stddef.h>

char *strdup(const char *s);
double strtod(const char *s, char **end);

#ifndef alloca
#define alloca(size) __builtin_alloca(size)
#endif

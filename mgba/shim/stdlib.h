#pragma once
#include_next <stdlib.h>
#ifndef MGBA_SHIM_DIV
#define MGBA_SHIM_DIV
typedef struct {
	int quot;
	int rem;
} div_t;
static inline div_t div(int n, int d)
{
	div_t r = {n / d, n % d};

	return r;
}
#endif
float strtof(const char *s, char **end);
char *getenv(const char *name);

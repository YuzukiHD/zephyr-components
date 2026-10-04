#pragma once
#include_next <time.h>
#include <stddef.h>
time_t mktime(struct tm *tm);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

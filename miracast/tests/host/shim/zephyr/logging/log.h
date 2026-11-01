/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#define LOG_LEVEL_INF 3
#define LOG_MODULE_REGISTER(name, level)
#define LOG_INF(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define LOG_WRN(...) LOG_INF(__VA_ARGS__)

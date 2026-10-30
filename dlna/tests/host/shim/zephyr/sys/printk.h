/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdio.h>
#define printk(...) fprintf(stderr, __VA_ARGS__)

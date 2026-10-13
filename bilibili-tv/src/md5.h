/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_MD5_H_
#define BILI_MD5_H_

#include <stddef.h>
#include <stdint.h>

/** MD5 of a buffer as 32 lower case hex characters and a NUL */
void md5_hex(const void *data, size_t len, char out[33]);

#endif /* BILI_MD5_H_ */

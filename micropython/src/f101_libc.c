/* Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 *
 * Gaps between the toolchain's libm and the minimal C library.
 */
#include <errno.h>

/* the errno of the toolchain's libm */
int *__errno(void)
{
	return &errno;
}

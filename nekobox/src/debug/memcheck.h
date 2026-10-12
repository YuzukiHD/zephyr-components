/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NEKOBOX_DEBUG_MEMCHECK_H_
#define NEKOBOX_DEBUG_MEMCHECK_H_

#include <stddef.h>

/*
 * How much of the malloc arena can be allocated now: the largest single block (what a decoder
 * needs) and the sum of all of it (what is left in total). The memory is taken and given back.
 */
void nekobox_mem_probe(size_t *largest, size_t *total);

#endif /* NEKOBOX_DEBUG_MEMCHECK_H_ */

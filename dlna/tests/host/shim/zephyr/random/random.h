/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdlib.h>
static inline uint32_t sys_rand32_get(void) { return (uint32_t)rand(); }

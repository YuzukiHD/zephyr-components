/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdatomic.h>
typedef _Atomic int atomic_t;
#define atomic_get(p) atomic_load(p)
#define atomic_set(p, v) atomic_store(p, v)

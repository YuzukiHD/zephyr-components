/* SPDX-License-Identifier: Apache-2.0 */
#include <stdatomic.h>
typedef atomic_int atomic_t;
#define atomic_get(p) atomic_load(p)
#define atomic_clear(p) atomic_store(p, 0)
#define atomic_set(p, value) atomic_store(p, value)

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOCHS_SHIM_PTHREAD_H_
#define BOCHS_SHIM_PTHREAD_H_
/* Bochs' thread helpers are not used on this target: single threaded no-ops */
typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_cond_t;
static inline int pthread_mutex_init(pthread_mutex_t *m, const void *a) { return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { return 0; }
#endif

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Samples where a thread spends its time; empty when MP5_PROFILE is off */

#ifndef MP5_DEBUG_PROFILE_H_
#define MP5_DEBUG_PROFILE_H_

#include <zephyr/kernel.h>

#ifdef CONFIG_MP5_PROFILE
void mp5_profile_start(struct k_thread *target);
#else
static inline void mp5_profile_start(struct k_thread *target) { (void)target; }
#endif

#endif /* MP5_DEBUG_PROFILE_H_ */

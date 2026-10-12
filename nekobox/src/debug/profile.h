/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Samples where a thread spends its time; empty when NEKOBOX_PROFILE is off */

#ifndef NEKOBOX_DEBUG_PROFILE_H_
#define NEKOBOX_DEBUG_PROFILE_H_

#include <zephyr/kernel.h>

#ifdef CONFIG_NEKOBOX_PROFILE
void nekobox_profile_start(struct k_thread *target);
#else
static inline void nekobox_profile_start(struct k_thread *target) { (void)target; }
#endif

#endif /* NEKOBOX_DEBUG_PROFILE_H_ */

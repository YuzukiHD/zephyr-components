/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Numbers about the user interface, printed when they happen; empty when NEKOBOX_DEBUG is off */

#ifndef NEKOBOX_DEBUG_STATS_H_
#define NEKOBOX_DEBUG_STATS_H_

#include <stdint.h>

#ifdef CONFIG_NEKOBOX_DEBUG
void ui_stats_init(void);
void ui_stats_scroll_begin(const char *name);
void ui_stats_scroll_end(void);
void ui_stats_page_begin(const char *name);
void ui_stats_page_set_up(uint32_t us);
void ui_stats_scroll_summary(void);
#else
static inline void ui_stats_init(void) {}
static inline void ui_stats_scroll_begin(const char *name) { (void)name; }
static inline void ui_stats_scroll_end(void) {}
static inline void ui_stats_page_begin(const char *name) { (void)name; }
static inline void ui_stats_page_set_up(uint32_t us) { (void)us; }
static inline void ui_stats_scroll_summary(void) {}
#endif

#endif /* NEKOBOX_DEBUG_STATS_H_ */

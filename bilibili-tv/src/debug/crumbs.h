/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_CRUMBS_H_
#define BILI_CRUMBS_H_

#ifdef CONFIG_BILI_CRUMBS
/** A mark (up to 7 characters) in the ring of the freeze recorder, see crumbs.c */
void bili_crumb(const char *what);
#else
#define bili_crumb(what) ((void)0)
#endif

#endif /* BILI_CRUMBS_H_ */

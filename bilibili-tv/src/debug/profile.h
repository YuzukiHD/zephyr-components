/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BILI_PROFILE_H_
#define BILI_PROFILE_H_

/**
 * Where the thread that calls this spends its time: a sampler thread looks at the saved program
 * counter of it every millisecond and counts the 128 byte blocks of code; the CPU share of every
 * thread is taken from the run time statistics. prof_report() prints both for the time since
 * prof_start(); map the addresses with `addr2line -f -e zephyr.elf`.
 */
void prof_start(void);
void prof_report(const char *what);

#endif /* BILI_PROFILE_H_ */

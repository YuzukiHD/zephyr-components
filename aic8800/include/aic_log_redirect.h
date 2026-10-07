/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Force-included into every file of the module: the output of the driver and the supplicant
 * (printf, printk, AIC_LOG_*, aic_dbg) goes to the Zephyr logging system, module "aic8800",
 * one log message per line.
 */
#ifndef AIC_LOG_REDIRECT_H
#define AIC_LOG_REDIRECT_H

/* the declarations come first, the macros below must not touch them */
#include <stdio.h>
#include <zephyr/sys/printk.h>

/* same values as LOG_LEVEL_ERR, LOG_LEVEL_WRN, LOG_LEVEL_INF and LOG_LEVEL_DBG */
#define AIC_LVL_ERR	1
#define AIC_LVL_WRN	2
#define AIC_LVL_INF	3
#define AIC_LVL_DBG	4

int aic_log(unsigned int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define printf(...)	aic_log(AIC_LVL_INF, __VA_ARGS__)
#define printk(...)	aic_log(AIC_LVL_INF, __VA_ARGS__)

#endif /* AIC_LOG_REDIRECT_H */

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Starting and stopping the AIC8800 chip and its driver
 *
 * The driver starts when it is first used (a scan, a join or the Bluetooth transport) and stays
 * until aic8800_stop(): that stops every task and timer of the driver, switches the chip off, and
 * gives back the memory the driver took, so that nothing of it is left running or allocated. The
 * next use starts it again, which takes the time of the firmware download.
 */

#ifndef AIC8800_AIC8800_H_
#define AIC8800_AIC8800_H_

#include <stdbool.h>
#include <stddef.h>

/** Starts the chip and the driver; 0 when it runs (it may have been running already) */
int aic8800_start(void);

/**
 * Stops the driver and powers the chip off. Not to be called while Bluetooth is enabled (disable
 * it first) or from a task of the driver. Returns 0 when it was running and is stopped.
 */
int aic8800_stop(void);

/** True from a start to the stop */
bool aic8800_running(void);

/** Memory the driver has allocated now and the most it had at one time, in bytes */
void aic8800_memory(size_t *used, size_t *peak);

#endif /* AIC8800_AIC8800_H_ */

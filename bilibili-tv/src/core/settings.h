/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Volume and brightness, kept in a file on the card and applied at start */

#ifndef BILI_SETTINGS_H_
#define BILI_SETTINGS_H_

#define BRIGHTNESS_MIN	10
#define BRIGHTNESS_MAX	255

/** Reads the file of the card and applies what is in it */
void settings_load(void);
/** Writes the file; blocks on the card, so not for the thread that draws */
void settings_save(void);

int settings_volume(void);		/**< 0..100 */
int settings_brightness(void);		/**< BRIGHTNESS_MIN..BRIGHTNESS_MAX */
/** Both apply the value at once; call settings_save() (through the worker) when it is final */
void settings_set_volume(int percent);
void settings_set_brightness(int value);

#endif /* BILI_SETTINGS_H_ */

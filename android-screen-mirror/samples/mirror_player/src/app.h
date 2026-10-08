/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_H_
#define APP_H_

#include <stdint.h>
#include <mirror_zephyr/scrcpy.h>

/* ui.c: status text, navigation keys and the touch relay */
void ui_start(void);
void ui_set_status(const char *text);
/** A QR code with the given text in the middle of the screen, until ui_hide_qr() */
void ui_show_qr(const char *payload);
void ui_hide_qr(void);
/** The session touches are sent to; NULL when there is none */
void ui_set_session(struct scrcpy *s);
/** Size of the picture on the screen, for the mapping of touches */
void ui_set_video_size(uint16_t w, uint16_t h);

#endif

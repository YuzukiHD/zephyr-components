/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_H_
#define APP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <mirror_zephyr/scrcpy.h>

/* ui.c: status text, navigation keys and the touch relay */
void ui_start(void);
void ui_set_status(const char *text);
/** A QR code with the given text in the middle of the screen, until ui_hide_qr() */
void ui_show_qr(const char *payload);
void ui_hide_qr(void);
/**
 * Shows the WiFi setup form (names, password, on-screen keyboard) and waits until the user
 * confirms; @p ssid and @p psk give the values to start with and take the result.
 */
int ui_wifi_prompt(char *ssid, size_t ssid_size, char *psk, size_t psk_size, const char *message);
/** The function the form calls to start a scan for networks (it must not block) */
void ui_wifi_set_scanner(void (*start)(void));
/** The networks found, one name per line (strongest first); updates the list of the form */
void ui_wifi_networks(const char *options);
/** What the user asked for in the settings menu (the mirror is interrupted for it) */
enum ui_action {
	UI_ACT_NONE,
	UI_ACT_CHANGE_WIFI,
	UI_ACT_PAIR,
};
/** The pending request, cleared by the call */
enum ui_action ui_take_action(void);
bool ui_action_pending(void);
/** The session touches are sent to; NULL when there is none */
void ui_set_session(struct scrcpy *s);
/** Size of the picture on the screen, for the mapping of touches */
void ui_set_video_size(uint16_t w, uint16_t h);

#endif

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The login of the user: the three cookie values of a scanned login, kept in a file on the card
 * and put into the Cookie header of the requests. The worker changes it, any thread may ask.
 */

#ifndef BILI_ACCOUNT_H_
#define BILI_ACCOUNT_H_

#include <stdbool.h>
#include <stddef.h>

/** Reads the file of the card, if there is one */
void account_load(void);

/**
 * Takes the cookies out of the answer of a confirmed scan login (an address with
 * ...DedeUserID=..&SESSDATA=..&bili_jct=.. in it, in the text as the server sent it) and saves
 * them. 0 or a negative errno.
 */
int account_set_from_text(const char *text);

/** Forgets the login and deletes the file */
void account_clear(void);

bool account_logged_in(void);

/** "SESSDATA=..; bili_jct=..; DedeUserID=.." for a Cookie header, "" without a login (copied) */
void account_cookie(char *out, size_t size);

/** The name shown on the account page; empty until the nav answer has been read */
void account_name(char *out, size_t size);
void account_set_name(const char *name);

#endif /* BILI_ACCOUNT_H_ */

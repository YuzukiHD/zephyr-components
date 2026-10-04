/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AIC_INET_H_
#define AIC_INET_H_

#include "lwip/sockets.h"

char *inet_ntoa(struct in_addr addr);
in_addr_t inet_addr(const char *cp);
int inet_aton(const char *cp, struct in_addr *addr);
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_pton(int af, const char *src, void *dst);

#endif /* AIC_INET_H_ */

/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IPERF3_H_
#define IPERF3_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * One TCP test against an iperf3 server: the client sends for @seconds, or with @reverse the
 * server sends. Returns 0 and the number of bytes moved by the data stream, or a negative errno.
 */
int iperf3_tcp(const char *server, uint16_t port, int seconds, bool reverse, uint64_t *bytes);

#endif /* IPERF3_H_ */

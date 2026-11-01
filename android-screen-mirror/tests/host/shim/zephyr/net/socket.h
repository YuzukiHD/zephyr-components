/* SPDX-License-Identifier: Apache-2.0 */
#ifndef TEST_ZEPHYR_SOCKET_H_
#define TEST_ZEPHYR_SOCKET_H_

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define zsock_socket socket
#define zsock_connect connect
#define zsock_close close
#define zsock_inet_pton inet_pton

#endif

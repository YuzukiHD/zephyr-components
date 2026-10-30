/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#define zsock_addrinfo addrinfo
#define zsock_pollfd pollfd
#define zsock_getaddrinfo getaddrinfo
#define zsock_freeaddrinfo freeaddrinfo
#define zsock_socket socket
#define zsock_fcntl fcntl
#define zsock_connect connect
#define zsock_getsockopt getsockopt
#define zsock_poll poll
#define zsock_close close
#define zsock_recv recv
#define zsock_send send
#define ZSOCK_POLLIN POLLIN
#define ZSOCK_POLLOUT POLLOUT
#define ZSOCK_POLLERR POLLERR
#define ZSOCK_POLLHUP POLLHUP

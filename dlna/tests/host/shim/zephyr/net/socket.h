/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#define zsock_pollfd pollfd
#define zsock_socket socket
#define zsock_fcntl fcntl
#define zsock_connect connect
#define zsock_poll poll
#define zsock_close close
#define zsock_recv recv
#define zsock_send send
#define zsock_bind bind
#define zsock_listen listen
#define zsock_accept accept
#define zsock_setsockopt setsockopt
#define zsock_getsockopt getsockopt
#define zsock_inet_pton inet_pton
#define ZSOCK_POLLIN POLLIN
#define ZSOCK_POLLOUT POLLOUT
#define ZSOCK_POLLERR POLLERR
#define ZSOCK_POLLHUP POLLHUP

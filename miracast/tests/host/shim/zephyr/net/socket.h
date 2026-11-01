/* SPDX-License-Identifier: Apache-2.0 */
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define zsock_socket socket
#define zsock_bind bind
#define zsock_close close
#define zsock_fcntl fcntl
#define zsock_connect connect
#define zsock_getsockopt getsockopt
#define zsock_send send
#define zsock_sendto sendto
#define zsock_recv recv
#define zsock_recvfrom recvfrom
#define zsock_poll poll
#define zsock_pollfd pollfd
#define zsock_inet_pton inet_pton
#define ZVFS_F_GETFL F_GETFL
#define ZVFS_F_SETFL F_SETFL
#define ZVFS_O_NONBLOCK O_NONBLOCK
#define ZSOCK_MSG_DONTWAIT MSG_DONTWAIT
#define ZSOCK_POLLIN POLLIN
#define ZSOCK_POLLOUT POLLOUT
#define ZSOCK_POLLHUP POLLHUP
#define ZSOCK_POLLERR POLLERR
#define ZSOCK_POLLNVAL POLLNVAL

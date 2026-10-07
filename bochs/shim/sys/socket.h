/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOCHS_SHIM_SYS_SOCKET_H_
#define BOCHS_SHIM_SYS_SOCKET_H_
/* declarations only: the serial port code of Bochs links against them, every call fails */
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned int socklen_t;
struct sockaddr { unsigned short sa_family; char sa_data[14]; };
#define AF_INET 2
#define SOCK_STREAM 1
#define SOMAXCONN 4
int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
ssize_t send(int fd, const void *buf, size_t n, int flags);
ssize_t recv(int fd, void *buf, size_t n, int flags);
#ifdef __cplusplus
}
#endif
#endif

/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOCHS_SHIM_NETINET_IN_H_
#define BOCHS_SHIM_NETINET_IN_H_
#include <stdint.h>
#include <sys/socket.h>
struct in_addr { uint32_t s_addr; };
struct sockaddr_in {
	unsigned short sin_family;
	unsigned short sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};
#define INADDR_ANY 0
#define htons(x) ((unsigned short)((((x) & 0xff) << 8) | (((x) >> 8) & 0xff)))
#define ntohs(x) htons(x)
#endif

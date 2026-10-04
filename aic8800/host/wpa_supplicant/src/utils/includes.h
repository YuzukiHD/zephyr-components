/*
 * wpa_supplicant/hostapd - Default include files
 * Copyright (c) 2005-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 *
 * This header file is included into all C files so that commonly used header
 * files can be selected with OS specific ifdef blocks in one place instead of
 * having to have OS/C library specific selection in many files.
 */

#ifndef INCLUDES_H
#define INCLUDES_H

/* Include possible build time configuration before including anything else */
#include "build_config.h"

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "aic_sock.h"
#ifdef PLATFORM_ALLWIN_RT_THREAD
#include <rtthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/types.h>
#include "rtos_errno.h"
#elif defined(PLATFORM_ALLWIN_FREERTOS)
#include "rtos_errno.h"
#endif
#ifndef _WIN32_WCE
#include <signal.h>
//#include <sys/types.h>
//#include <errno.h>
#endif /* _WIN32_WCE */

#ifndef _MSC_VER
//#include <unistd.h>
#endif /* _MSC_VER */

#include "aic_log.h"

#ifdef CONFIG_RWNX
#include "rtos_al.h"
/* the supplicant does not use the network interface layer */
//#include "cmd_def.h"
#include "compiler.h"
//#define vsnprintf dbg_vsnprintf
#ifndef isprint
#define isprint(c)      ((c >= 0x1F) && (c <= 0x7E))
#define isblank(c)      ((c) == ' ' || (c) == '\t')
#define isspace(c)      ((c) == ' ' || (c) == '\f' || (c) == '\n' || (c) == '\r' || (c) == '\t' || (c) == '\v')
#endif
#endif /* CONFIG_RWNX */

#if defined(CONFIG_LWIP)

#include <lwip/sockets.h>
#include <lwip/inet.h>

//#else /* ! CONFIG_LWIP */
#ifndef CONFIG_NATIVE_WINDOWS
#include <sys/socket.h>
//#include <netinet/in.h>
//#include <arpa/inet.h>
#ifndef __vxworks
//#include <sys/uio.h>
//#include <sys/time.h>
#endif /* __vxworks */
#endif /* CONFIG_NATIVE_WINDOWS */
#endif /* CONFIG_LWIP */

/* Big-endian type definitions for non-Linux platforms */
#ifndef be16
typedef uint16_t be16;
#endif
#ifndef be32
typedef uint32_t be32;
#endif

#endif /* INCLUDES_H */

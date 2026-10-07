/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOCHS_SHIM_SIGNAL_H_
#define BOCHS_SHIM_SIGNAL_H_
/* there are no signals: the handlers are never called */
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIGINT 2
#define SIGALRM 14
#define SIGPIPE 13
static inline sighandler_t signal(int sig, sighandler_t h) { return SIG_DFL; }
static inline unsigned int alarm(unsigned int s) { return 0; }
#ifdef __cplusplus
}
#endif
#endif

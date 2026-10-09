/* SPDX-License-Identifier: Apache-2.0 */
/* The few helpers of quickjs-libc the guest uses; quickjs-libc.c needs a POSIX
 * system. */
#pragma once

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

void js_std_init_handlers(JSRuntime *rt);
void js_std_free_handlers(JSRuntime *rt);
void js_std_add_helpers(JSContext *ctx, int argc, char **argv);
void js_std_dump_error(JSContext *ctx);

#ifdef __cplusplus
}
#endif

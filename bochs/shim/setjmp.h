/* SPDX-License-Identifier: Apache-2.0 */
#ifndef BOCHS_SHIM_SETJMP_H_
#define BOCHS_SHIM_SETJMP_H_
#ifdef __cplusplus
extern "C" {
#endif
/* ra, sp, s0..s11 and fs0..fs11 (src/bochs_setjmp.S) */
typedef unsigned long jmp_buf[14 + 12 * 2];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif

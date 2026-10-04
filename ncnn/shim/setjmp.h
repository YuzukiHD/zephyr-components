#ifndef NCNN_SHIM_SETJMP_H
#define NCNN_SHIM_SETJMP_H
typedef long jmp_buf[32];
#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf);
void longjmp(jmp_buf, int);
#ifdef __cplusplus
}
#endif
#endif

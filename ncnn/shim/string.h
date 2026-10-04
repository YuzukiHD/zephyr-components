#ifndef NCNN_SHIM_STRING_H
#define NCNN_SHIM_STRING_H
#include_next <string.h>
#ifdef __cplusplus
extern "C" {
#endif
char *strtok(char *, const char *);
#ifdef __cplusplus
}
#endif
#endif

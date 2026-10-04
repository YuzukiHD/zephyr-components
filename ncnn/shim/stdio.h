#ifndef NCNN_SHIM_STDIO_H
#define NCNN_SHIM_STDIO_H
#include_next <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
int sscanf(const char *str, const char *format, ...);

/* file access of ncnn; networks are loaded from memory, these always fail */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
int feof(FILE *f);
int fgetc(FILE *f);
char *fgets(char *s, int size, FILE *f);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f);
int fscanf(FILE *f, const char *format, ...);
#ifdef __cplusplus
}
#endif
#endif

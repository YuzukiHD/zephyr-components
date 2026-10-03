#pragma once
#include_next <stdio.h>
typedef struct mgba_file FILE_;
char *fgets(char *s, int n, FILE *f);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);

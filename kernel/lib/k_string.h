#ifndef KERNEL_LIB_K_STRING_H
#define KERNEL_LIB_K_STRING_H

#include <stddef.h>

size_t k_strlen(const char *str);

void k_strcpy(char *dst, const char *src);

int k_strcmp(const char *a, const char *b);

#endif
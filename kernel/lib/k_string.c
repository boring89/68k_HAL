#include "k_string.h"

size_t k_strlen(const char *str)
{
    size_t len = 0;

    while (str[len])
        len++;

    return len;
}

void k_strcpy(char *dst, const char *src)
{
    while (*src)
    {
        *dst++ = *src++;
    }

    *dst = '\0'
}

int k_strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b))
    {
        a++;
        b++;
    }

    return *(unsigned char *)a -
        *(unsigned char *)b;
}
/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Library: qsort, bsearch, string and number conversion functions. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;

    return (x > y) - (x < y);
}

int main(void)
{
    int values[] = {42, 7, 19, -3, 100, 0, 56, 7, 88, -50};
    size_t count = sizeof(values) / sizeof(values[0]);

    qsort(values, count, sizeof(values[0]), compare_int);

    printf("sorted:");

    for (size_t i = 0; i < count; i++)
        printf(" %d", values[i]);

    printf("\n");

    int key = 56;
    int *found = bsearch(&key, values, count, sizeof(values[0]), compare_int);

    printf("bsearch: %s\n", found != NULL ? "found" : "missing");

    char text[] = "The Quick Brown Fox";

    for (char *p = text; *p != '\0'; p++)
        *p = (char)toupper((unsigned char)*p);

    printf("upper: %s\n", text);
    printf("strtol: %ld %ld %ld\n", strtol("1234", NULL, 10), strtol("-0x1f", NULL, 16),
           strtol("777", NULL, 8));
    printf("strtod: %.3f\n", strtod("2.718281828", NULL));
    printf("strstr: %s\n", strstr("find the needle here", "needle"));
    printf("strtok:");

    char words[] = "alpha,beta;gamma";

    for (char *w = strtok(words, ",;"); w != NULL; w = strtok(NULL, ",;"))
        printf(" [%s]", w);

    printf("\n");
    printf("rand: %d\n", (srand(1), rand() % 1000) >= 0);

    return 0;
}

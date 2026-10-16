/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Memory: malloc, free, realloc, calloc, big blocks (mmap) and a stack buffer. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned int checksum(const unsigned char *p, size_t n)
{
    unsigned int sum = 0;

    for (size_t i = 0; i < n; i++)
        sum = sum * 31 + p[i];

    return sum;
}

int main(void)
{
    enum { COUNT = 200 };
    unsigned char *blocks[COUNT];
    unsigned int sums[COUNT];

    for (int i = 0; i < COUNT; i++)
    {
        size_t size = 16 + (size_t)i * 37;

        blocks[i] = malloc(size);

        if (blocks[i] == NULL)
        {
            printf("malloc failed at %d\n", i);
            return 1;
        }

        memset(blocks[i], i, size);
        sums[i] = checksum(blocks[i], size);
    }

    int bad = 0;

    for (int i = 0; i < COUNT; i++)
    {
        if (checksum(blocks[i], 16 + (size_t)i * 37) != sums[i])
            bad++;
    }

    printf("small blocks: %d bad\n", bad);

    for (int i = 0; i < COUNT; i += 2)
        free(blocks[i]);

    /* A block bigger than the heap's mmap threshold. */
    size_t big_size = 3u * 1024 * 1024;
    unsigned char *big = malloc(big_size);

    if (big == NULL)
    {
        printf("big malloc failed\n");
        return 1;
    }

    memset(big, 0xA5, big_size);
    printf("big block: %u\n", checksum(big, big_size));

    big = realloc(big, big_size * 2);
    printf("after realloc: %u\n", checksum(big, big_size));
    free(big);

    int *zeros = calloc(1000, sizeof(int));
    long total = 0;

    for (int i = 0; i < 1000; i++)
        total += zeros[i];

    printf("calloc: %ld\n", total);
    free(zeros);

    for (int i = 1; i < COUNT; i += 2)
        free(blocks[i]);

    char stack_buffer[100000];

    memset(stack_buffer, 7, sizeof(stack_buffer));
    printf("stack: %u\n", checksum((unsigned char *)stack_buffer, sizeof(stack_buffer)));

    return 0;
}

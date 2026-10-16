/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Standard input: the console hands over one line at a time. Interactive; not in the self test. */

#include <stdio.h>
#include <string.h>

int main(void)
{
    char line[128];

    printf("Type something: ");
    fflush(stdout);

    if (fgets(line, sizeof(line), stdin) == NULL)
    {
        printf("end of input\n");
        return 1;
    }

    line[strcspn(line, "\n")] = '\0';
    printf("you typed: %s (%zu characters)\n", line, strlen(line));

    return 0;
}

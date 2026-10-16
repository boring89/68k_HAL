/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Arguments and environment, as the shell's run command passes them. */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    printf("argc: %d\n", argc);

    for (int i = 0; i < argc; i++)
        printf("argv[%d]: %s\n", i, argv[i]);

    const char *path = getenv("PATH");

    printf("PATH: %s\n", path != NULL ? path : "(unset)");
    printf("missing: %s\n", getenv("NO_SUCH_VARIABLE") != NULL ? "set" : "unset");

    return argc > 1 ? 3 : 0;
}

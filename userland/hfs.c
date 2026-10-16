/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The Mac disk: the HFS volume shows up as /hfs (read-only; test image only). */

#include <dirent.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>

int main(void)
{
    FILE *file = fopen("/hfs/ReadMe", "r");

    if (file == NULL)
    {
        printf("no /hfs/ReadMe\n");
        return 1;
    }

    char text[64];
    size_t n = fread(text, 1, sizeof(text) - 1, file);

    fclose(file);
    text[n] = '\0';

    /* Mac text ends lines with CR. */
    for (size_t i = 0; i < n; i++)
    {
        if (text[i] == '\r')
            text[i] = '\n';
    }

    printf("ReadMe: %s", text);

    struct stat st;

    printf("leaf: %s\n", stat("/hfs/docs/deep/deeper/leaf", &st) == 0 ? "found" : "missing");
    printf("docs is a folder: %d\n", stat("/hfs/docs", &st) == 0 && S_ISDIR(st.st_mode));

    DIR *dir = opendir("/hfs");
    struct dirent *entry;
    int found_docs = 0;
    int found_readme = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        found_docs |= strcasecmp(entry->d_name, "docs") == 0;
        found_readme |= strcmp(entry->d_name, "ReadMe") == 0;
    }

    closedir(dir);
    printf("listing has docs: %d, ReadMe: %d\n", found_docs, found_readme);

    return 0;
}

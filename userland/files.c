/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Files: read the program disk, write /tmp and read it back, directories. */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char line[128];
    FILE *in = fopen("/data/message.txt", "r");

    if (in == NULL)
    {
        printf("cannot open message: %s\n", strerror(errno));
        return 1;
    }

    while (fgets(line, sizeof(line), in) != NULL)
        printf("message: %s", line);

    fclose(in);

    struct stat st;

    if (stat("/data/message.txt", &st) == 0)
        printf("size: %ld regular: %d\n", (long)st.st_size, S_ISREG(st.st_mode) != 0);

    /* Write /tmp and read it back. */
    if (mkdir("/tmp/work", 0755) != 0)
        printf("mkdir: %s\n", strerror(errno));

    FILE *out = fopen("/tmp/work/numbers.txt", "w");

    for (int i = 1; i <= 5; i++)
        fprintf(out, "%d squared is %d\n", i, i * i);

    fclose(out);

    in = fopen("/tmp/work/numbers.txt", "r");

    while (fgets(line, sizeof(line), in) != NULL)
        printf("tmp: %s", line);

    fclose(in);

    /* Seek and read in the middle. */
    int fd = open("/tmp/work/numbers.txt", O_RDONLY);
    char piece[8] = {0};

    lseek(fd, 2, SEEK_SET);
    read(fd, piece, 7);
    printf("piece: [%s]\n", piece);
    close(fd);

    /* Missing file and read-only disk give the right errors. */
    errno = 0;

    if (open("/no/such/file", O_RDONLY) < 0)
        printf("missing: %s\n", strerror(errno));

    if (open("/data/new.txt", O_WRONLY | O_CREAT, 0644) < 0)
        printf("read-only: %s\n", strerror(errno));

    /* List a directory. */
    DIR *dir = opendir("/tmp/work");
    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_name[0] != '.')
            printf("entry: %s\n", entry->d_name);

        count++;
    }

    closedir(dir);
    printf("entries: %d\n", count);

    unlink("/tmp/work/numbers.txt");
    rmdir("/tmp/work");
    printf("removed: %d\n", access("/tmp/work", F_OK));

    return 0;
}

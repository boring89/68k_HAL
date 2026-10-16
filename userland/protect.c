/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Page protection: read-only and unreachable pages, and what the kernel does with pointers into them. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const char constant[] = "I live in read-only memory";

int main(void)
{
    char *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    strcpy(page, "writable page");
    printf("%s\n", page);

    /* The kernel must not write into read-only memory for the program: read() gives EFAULT. */
    int fd = open("/data/message.txt", O_RDONLY);

    mprotect(page, 4096, PROT_READ);
    errno = 0;
    ssize_t n = read(fd, page, 10);

    printf("read into a read-only page: %zd (%s)\n", n, strerror(errno));

    mprotect(page, 4096, PROT_READ | PROT_WRITE);
    lseek(fd, 0, SEEK_SET);
    printf("read into it after mprotect(RW): %zd\n", read(fd, page, 10));

    /* A page the program cannot touch at all: not even the kernel reads it for the program. */
    mprotect(page, 4096, PROT_NONE);
    errno = 0;
    n = write(1, page, 4);
    printf("write() from an unreachable page: %zd (%s)\n", n, strerror(errno));
    mprotect(page, 4096, PROT_READ | PROT_WRITE);

    /* A mapping that was never writable. */
    char *ro = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    printf("read-only mapping reads as %d\n", ro[100]);
    close(fd);
    fflush(stdout);

    /* Constants cannot be changed: this write kills the program (exit status 139). */
    char *victim = (char *)constant;

    victim[0] = 'X';
    printf("not reached: %s\n", constant);

    return 0;
}

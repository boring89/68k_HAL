/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* pipe, dup2, redirection through exec, end of file, SIGPIPE. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    int fds[2];
    int status;

    /* A child writes, the parent reads until end of file. */
    pipe(fds);
    fflush(stdout);

    if (fork() == 0)
    {
        close(fds[0]);

        for (int i = 1; i <= 5; i++)
        {
            char line[16];
            int n = snprintf(line, sizeof(line), "line %d\n", i);

            write(fds[1], line, (size_t)n);
        }

        _exit(0);
    }

    close(fds[1]);

    char buffer[256];
    size_t total = 0;
    ssize_t n;

    while ((n = read(fds[0], buffer + total, sizeof(buffer) - total)) > 0)
        total += (size_t)n;

    close(fds[0]);
    wait(&status);
    printf("read %zu bytes, %d lines, last: %s", total, 5, strrchr(buffer, 'l') != NULL ? strrchr(buffer, 'l') : "?");

    /* More than the pipe holds: the writer waits for the reader. */
    pipe(fds);
    fflush(stdout);

    if (fork() == 0)
    {
        close(fds[0]);

        char block[1000];

        memset(block, 'x', sizeof(block));

        for (int i = 0; i < 100; i++)
            write(fds[1], block, sizeof(block));

        _exit(0);
    }

    close(fds[1]);

    long count = 0;
    char chunk[512];

    while ((n = read(fds[0], chunk, sizeof(chunk))) > 0)
        count += n;

    close(fds[0]);
    wait(&status);
    printf("a writer of 100000 bytes was read as %ld bytes\n", count);

    /* The child's standard output goes into a pipe (dup2) while it runs another program. */
    pipe(fds);
    fflush(stdout);

    if (fork() == 0)
    {
        dup2(fds[1], 1);
        close(fds[0]);
        close(fds[1]);
        execl("/bin/hello", "hello", (char *)NULL);
        _exit(127);
    }

    close(fds[1]);
    n = read(fds[0], buffer, sizeof(buffer) - 1);
    buffer[n > 0 ? n : 0] = '\0';
    close(fds[0]);
    wait(&status);
    printf("hello said: %s", buffer);

    /* Writing to a pipe nobody reads: EPIPE (SIGPIPE ignored). */
    signal(SIGPIPE, SIG_IGN);
    pipe(fds);
    close(fds[0]);

    ssize_t written = write(fds[1], "x", 1);

    printf("write to a closed pipe: %zd, %s\n", written, errno == EPIPE ? "EPIPE" : "other");
    close(fds[1]);

    /* And with the default action the writer is killed by SIGPIPE. */
    fflush(stdout);

    if (fork() == 0)
    {
        signal(SIGPIPE, SIG_DFL);
        pipe(fds);
        close(fds[0]);
        write(fds[1], "x", 1);
        _exit(0);
    }

    wait(&status);
    printf("default SIGPIPE: killed %d by signal %d\n", WIFSIGNALED(status), WIFSIGNALED(status) ? WTERMSIG(status) : 0);

    return 0;
}

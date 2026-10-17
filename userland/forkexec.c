/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* fork, exec, wait and exit status: the processes' life cycle. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int value = 5;

int main(void)
{
    /* A child gets its own copy of the memory. */
    fflush(stdout);
    pid_t pid = fork();

    if (pid == 0)
    {
        value = 99;
        printf("child: value %d, parent is %s\n", value, getppid() > 0 ? "alive" : "init");
        fflush(stdout);
        _exit(7);
    }

    int status = 0;

    if (waitpid(pid, &status, 0) != pid)
        printf("waitpid failed\n");

    printf("parent: value %d, child exited %d with %d\n", value, WIFEXITED(status), WEXITSTATUS(status));

    /* exec: the child becomes another program. */
    fflush(stdout);
    pid = fork();

    if (pid == 0)
    {
        execl("/bin/hello", "hello", (char *)NULL);
        printf("exec failed\n");
        _exit(127);
    }

    waitpid(pid, &status, 0);
    printf("exec'd program exited with %d\n", WEXITSTATUS(status));

    /* exec of a file that does not exist reports the error to the caller. */
    fflush(stdout);
    pid = fork();

    if (pid == 0)
    {
        execl("/bin/no-such-program", "x", (char *)NULL);
        _exit(errno == ENOENT ? 42 : 43);
    }

    waitpid(pid, &status, 0);
    printf("missing program: child exited with %d\n", WEXITSTATUS(status));

    /* Several children, collected in order of the numbers they exit with. */
    fflush(stdout);

    for (int i = 1; i <= 3; i++)
    {
        pid = fork();

        if (pid == 0)
            _exit(10 * i);
    }

    int total = 0;
    int children = 0;

    while ((pid = wait(&status)) > 0)
    {
        total += WEXITSTATUS(status);
        children++;
    }

    printf("collected %d children, status sum %d, then errno %s\n", children, total,
           errno == ECHILD ? "ECHILD" : "other");

    /* A grandchild: exec of a program that forks itself. */
    fflush(stdout);
    pid = fork();

    if (pid == 0)
    {
        pid_t inner = fork();

        if (inner == 0)
            _exit(3);

        int inner_status;

        waitpid(inner, &inner_status, 0);
        _exit(WEXITSTATUS(inner_status) + 1);
    }

    waitpid(pid, &status, 0);
    printf("grandchild chain: %d\n", WEXITSTATUS(status));

    return 0;
}

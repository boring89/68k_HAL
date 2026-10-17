/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Preemption: a program that computes for ever does not block the others, and can be killed. */

#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    int status;

    fflush(stdout);

    pid_t spinner = fork();

    if (spinner == 0)
    {
        /* Never calls the kernel. */
        for (volatile unsigned long i = 0;; i++)
            ;
    }

    /* The parent still gets its turn: a sleep ends on time although the child never gives up the CPU. */
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    usleep(100000);
    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;

    printf("sleep took %s\n", elapsed_ms >= 100 && elapsed_ms < 1000 ? "about as long as asked" : "the wrong time");

    /* Kill the spinner: it never enters the kernel, so the signal must reach it through the timer interrupt. */
    kill(spinner, SIGKILL);
    waitpid(spinner, &status, 0);
    printf("spinner: signaled %d, signal %d\n", WIFSIGNALED(status), WTERMSIG(status));

    /* Two spinning children and a parent that waits for them with a timeout (alarm-less: poll with WNOHANG). */
    pid_t a = fork();

    if (a == 0)
    {
        for (volatile unsigned long i = 0; i < 30000000ul; i++)
            ;

        _exit(5);
    }

    pid_t b = fork();

    if (b == 0)
    {
        for (volatile unsigned long i = 0; i < 30000000ul; i++)
            ;

        _exit(6);
    }

    int total = 0;

    for (int i = 0; i < 2; i++)
    {
        pid_t done = wait(&status);

        if (done == a || done == b)
            total += WEXITSTATUS(status);
    }

    printf("both finished: %d\n", total);

    return 0;
}

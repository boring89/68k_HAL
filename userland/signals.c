/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Signal handlers, masks, kill between processes, and a child killed while it waits. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t usr1_count;
static volatile sig_atomic_t chld_count;

static void on_usr1(int sig)
{
    (void)sig;
    usr1_count++;
}

static void on_chld(int sig)
{
    (void)sig;
    chld_count++;
}

int main(void)
{
    int status;

    /* A handler runs, and the program goes on where it was. */
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_usr1;
    sigaction(SIGUSR1, &sa, NULL);

    kill(getpid(), SIGUSR1);
    printf("after kill: handler ran %d time(s)\n", (int)usr1_count);

    /* Blocked: it waits until unblocked. */
    sigset_t block;
    sigset_t pending;

    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, NULL);
    kill(getpid(), SIGUSR1);
    sigpending(&pending);
    printf("blocked: count %d, pending %d\n", (int)usr1_count, sigismember(&pending, SIGUSR1));
    sigprocmask(SIG_UNBLOCK, &block, NULL);
    printf("unblocked: count %d\n", (int)usr1_count);

    /* SIGCHLD when a child ends. */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_chld;
    sigaction(SIGCHLD, &sa, NULL);
    fflush(stdout);

    if (fork() == 0)
        _exit(0);

    wait(&status);
    printf("SIGCHLD handler ran %d time(s)\n", (int)chld_count);

    /* A child that sleeps is killed by the parent. */
    signal(SIGCHLD, SIG_DFL);
    fflush(stdout);

    pid_t pid = fork();

    if (pid == 0)
    {
        for (;;)
            pause();
    }

    usleep(50000);
    kill(pid, SIGTERM);
    waitpid(pid, &status, 0);
    printf("sleeping child: signaled %d, signal %d\n", WIFSIGNALED(status), WTERMSIG(status));

    /* A child with a handler is interrupted out of pause() and exits normally. */
    fflush(stdout);
    pid = fork();

    if (pid == 0)
    {
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_usr1;
        sigaction(SIGUSR1, &sa, NULL);

        int before = usr1_count;

        pause();
        _exit(usr1_count == before + 1 ? 21 : 22);
    }

    usleep(50000);
    kill(pid, SIGUSR1);
    waitpid(pid, &status, 0);
    printf("handler child: exit %d\n", WEXITSTATUS(status));

    /* SIGKILL cannot be caught. */
    fflush(stdout);
    pid = fork();

    if (pid == 0)
    {
        signal(SIGKILL, SIG_IGN);

        for (;;)
            usleep(10000);
    }

    usleep(50000);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    printf("SIGKILL: signal %d\n", WTERMSIG(status));

    /* kill on a process that does not exist. */
    printf("kill of a missing process: %d\n", kill(30000, 0));

    return 0;
}

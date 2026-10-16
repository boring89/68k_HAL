/* Compiled inside the OS by tcc (self test), then run. */
#include <stdio.h>
#include <string.h>

static int fib(int n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main(int argc, char **argv)
{
    long long big = 1LL << 40;

    printf("compiled by tcc: fib(20)=%d %lld %s %.2f\n", fib(20), big / 1000, "ok", 3.14159 * 2);
    printf("args: %d\n", argc);

    return argc;
}

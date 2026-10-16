/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* A web client with BSD sockets: httpget <ipv4> <port> <path> prints the answer's body. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: httpget <ipv4> <port> <path>\n");
        return 2;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {0};

    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)atoi(argv[2]));

    if (inet_pton(AF_INET, argv[1], &address.sin_addr) != 1)
    {
        fprintf(stderr, "bad address\n");
        return 2;
    }

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        perror("connect");
        return 1;
    }

    char request[256];
    int length = snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", argv[3], argv[1]);

    if (write(fd, request, (size_t)length) != length)
    {
        perror("write");
        return 1;
    }

    char buffer[512];
    char *response = malloc(1);
    size_t have = 0;
    ssize_t n;

    response[0] = '\0';

    while ((n = read(fd, buffer, sizeof(buffer))) > 0)
    {
        response = realloc(response, have + (size_t)n + 1);
        memcpy(response + have, buffer, (size_t)n);
        have += (size_t)n;
        response[have] = '\0';
    }

    close(fd);

    /* The body follows the first empty line. */
    char *body = strstr(response, "\r\n\r\n");

    if (strncmp(response, "HTTP/1.", 7) != 0 || body == NULL)
    {
        fprintf(stderr, "not an HTTP answer (%zu bytes)\n", have);
        return 1;
    }

    printf("status: %.12s\n", response + 9);
    fputs(body + 4, stdout);

    return 0;
}

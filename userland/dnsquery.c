/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* UDP sockets and getaddrinfo: ask the DNS server by hand, then let musl's resolver do it from /etc/hosts. */

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    /* A DNS query for "localhost", type A, class IN. */
    unsigned char query[] = {0x12, 0x34, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0,
                             9, 'l', 'o', 'c', 'a', 'l', 'h', 'o', 's', 't', 0, 0, 1, 0, 1};
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in server = {0};

    server.sin_family = AF_INET;
    server.sin_port = htons(53);
    inet_pton(AF_INET, "10.0.2.3", &server.sin_addr);

    if (sendto(fd, query, sizeof(query), 0, (struct sockaddr *)&server, sizeof(server)) != (ssize_t)sizeof(query))
    {
        perror("sendto");
        return 1;
    }

    struct pollfd p = {fd, POLLIN, 0};

    if (poll(&p, 1, 5000) != 1 || !(p.revents & POLLIN))
    {
        printf("no answer\n");
        return 1;
    }

    unsigned char answer[512];
    ssize_t n = recvfrom(fd, answer, sizeof(answer), 0, NULL, NULL);

    printf("answer: %s id=%02x%02x\n", n > 12 ? "yes" : "short", answer[0], answer[1]);
    close(fd);

    /* The resolver of the C library: /etc/hosts. */
    struct addrinfo hints = {0};
    struct addrinfo *result;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("localhost", NULL, &hints, &result) != 0)
    {
        printf("getaddrinfo failed\n");
        return 1;
    }

    char text[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &((struct sockaddr_in *)result->ai_addr)->sin_addr, text, sizeof(text));
    printf("localhost is %s\n", text);
    freeaddrinfo(result);

    return 0;
}

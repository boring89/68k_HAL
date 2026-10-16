/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/net/packet.h"
#include "kernel/net/tcp.h"

#include <stdlib.h>
#include <string.h>

/*
 * Two TCP stacks (A = 10.0.0.1, B = 10.0.0.2) joined by a "wire" that can lose
 * segments, and a clock that only moves when the test says so.
 */
static const uint8_t ip_a[4] = {10, 0, 0, 1};
static const uint8_t ip_b[4] = {10, 0, 0, 2};

typedef struct
{
    uint8_t data[2048];
    size_t length;
    int to_b; /* 1 = for B, 0 = for A */
} packet_t;

#define QUEUE 256

static tcp_stack_t stack_a;
static tcp_stack_t stack_b;
static packet_t queue[QUEUE];
static int queue_head;
static int queue_count;
static uint64_t clock_ms;
static int sent_count;
static int drop_every;        /* drop every n-th segment (0 = none) */
static int drop_all;          /* 1: nothing gets through */
static int drop_syn_count;    /* drop this many SYNs at the start */
static int drop_next_to_a;    /* drop the next segment sent by B (for example a window update) */

static uint64_t now_ms(void *context)
{
    (void)context;

    return clock_ms;
}

static int wire_send(void *context, const uint8_t destination_ip[4], const uint8_t *segment, size_t length)
{
    int from_a = context == &stack_a;

    (void)destination_ip;
    sent_count++;

    if (drop_all)
        return 0;

    if (drop_every > 0 && sent_count % drop_every == 0)
        return 0;

    if (!from_a && drop_next_to_a > 0)
    {
        drop_next_to_a--;

        return 0;
    }

    if (drop_syn_count > 0 && (segment[13] & NET_TCP_SYN))
    {
        drop_syn_count--;

        return 0;
    }

    if (queue_count == QUEUE || length > sizeof(queue[0].data))
        return -1;

    packet_t *p = &queue[(queue_head + queue_count++) % QUEUE];

    memcpy(p->data, segment, length);
    p->length = length;
    p->to_b = from_a;

    return 0;
}

static void setup(void)
{
    tcp_env_t env_a = {&stack_a, wire_send, now_ms, {10, 0, 0, 1}};
    tcp_env_t env_b = {&stack_b, wire_send, now_ms, {10, 0, 0, 2}};

    clock_ms = 1000;
    queue_head = queue_count = 0;
    sent_count = 0;
    drop_every = 0;
    drop_all = 0;
    drop_syn_count = 0;
    drop_next_to_a = 0;
    tcp_init(&stack_a, &env_a);
    tcp_init(&stack_b, &env_b);
}

/* Deliver everything on the wire, then let a millisecond pass. */
static void step(void)
{
    while (queue_count > 0)
    {
        packet_t p = queue[queue_head];

        queue_head = (queue_head + 1) % QUEUE;
        queue_count--;

        if (p.to_b)
            tcp_input(&stack_b, ip_a, p.data, p.length);
        else
            tcp_input(&stack_a, ip_b, p.data, p.length);
    }

    clock_ms++;
    tcp_tick(&stack_a);
    tcp_tick(&stack_b);
}

static void run_ms(int ms)
{
    for (int i = 0; i < ms; i++)
        step();
}

/* Connect A to B's port 80; returns A's connection and sets *accepted to B's. */
static int open_connection(int *accepted)
{
    int listener = tcp_listen(&stack_b, 80);
    int client = tcp_connect(&stack_a, ip_b, 80);

    TEST_ASSERT(listener >= 0 && client >= 0);

    *accepted = -1;

    for (int i = 0; i < 20000 && *accepted < 0; i++)
    {
        step();
        *accepted = tcp_accept(&stack_b, listener);
    }

    return client;
}

static void test_handshake_and_data(void)
{
    setup();

    int server;
    int client = open_connection(&server);

    TEST_ASSERT(server >= 0);
    run_ms(5);
    TEST_ASSERT_EQ_INT(TCP_ESTABLISHED, tcp_state(&stack_a, client));
    TEST_ASSERT_EQ_INT(TCP_ESTABLISHED, tcp_state(&stack_b, server));

    /* A request and a reply. */
    char buffer[64];

    TEST_ASSERT_EQ_INT(5, tcp_send(&stack_a, client, "hello", 5));
    run_ms(5);
    TEST_ASSERT_EQ_INT(5, tcp_recv(&stack_b, server, buffer, sizeof(buffer)));
    TEST_ASSERT(memcmp(buffer, "hello", 5) == 0);
    TEST_ASSERT_EQ_INT(0, tcp_recv(&stack_b, server, buffer, sizeof(buffer)));

    TEST_ASSERT_EQ_INT(7, tcp_send(&stack_b, server, "goodbye", 7));
    run_ms(5);
    TEST_ASSERT_EQ_INT(7, tcp_recv(&stack_a, client, buffer, sizeof(buffer)));
    TEST_ASSERT(memcmp(buffer, "goodbye", 7) == 0);

    /* Close from A: B sees the end of the stream after reading what is left. */
    tcp_close(&stack_a, client);
    run_ms(5);
    TEST_ASSERT_EQ_INT(TCP_FIN_WAIT_2, tcp_state(&stack_a, client));
    TEST_ASSERT_EQ_INT(TCP_CLOSE_WAIT, tcp_state(&stack_b, server));
    TEST_ASSERT_EQ_INT(TCP_EOF, tcp_recv(&stack_b, server, buffer, sizeof(buffer)));
    TEST_ASSERT_EQ_INT(-1, tcp_send(&stack_a, client, "x", 1)); /* closed for sending */

    /* B can still send, then closes too. */
    TEST_ASSERT_EQ_INT(3, tcp_send(&stack_b, server, "bye", 3));
    tcp_close(&stack_b, server);
    run_ms(5);
    TEST_ASSERT_EQ_INT(3, tcp_recv(&stack_a, client, buffer, sizeof(buffer)));
    TEST_ASSERT_EQ_INT(TCP_EOF, tcp_recv(&stack_a, client, buffer, sizeof(buffer)));
    TEST_ASSERT_EQ_INT(TCP_CLOSED, tcp_state(&stack_b, server));
    TEST_ASSERT_EQ_INT(TCP_TIME_WAIT, tcp_state(&stack_a, client));

    run_ms(2100); /* TIME_WAIT is over */
    TEST_ASSERT_EQ_INT(TCP_CLOSED, tcp_state(&stack_a, client));

    tcp_abort(&stack_a, client);
    tcp_abort(&stack_b, server);
}

/* Send size bytes of a known pattern from A to B and check them. */
static int transfer(size_t size, int ms_limit)
{
    int server;
    int client = open_connection(&server);
    uint8_t *source = malloc(size);
    uint8_t *received = malloc(size);
    size_t sent = 0;
    size_t got = 0;
    int ms = 0;

    for (size_t i = 0; i < size; i++)
        source[i] = (uint8_t)(i * 7 + i / 251);

    while (got < size && ms++ < ms_limit)
    {
        if (sent < size)
        {
            int n = tcp_send(&stack_a, client, source + sent, size - sent);

            if (n > 0)
                sent += (size_t)n;
        }

        step();

        int n = tcp_recv(&stack_b, server, received + got, size - got);

        if (n > 0)
            got += (size_t)n;
    }

    int ok = got == size && memcmp(source, received, size) == 0;

    free(source);
    free(received);
    tcp_abort(&stack_a, client);
    tcp_abort(&stack_b, server);

    return ok;
}

static void test_large_transfer(void)
{
    setup();
    TEST_ASSERT(transfer(300000, 20000));
}

static void test_loss_is_repaired(void)
{
    setup();
    drop_every = 7; /* about 14% of the segments get lost, including SYNs and ACKs */
    TEST_ASSERT(transfer(60000, 400000));
    TEST_ASSERT(sent_count > 100);
}

static void test_syn_is_retransmitted(void)
{
    setup();
    drop_syn_count = 2;

    int server;
    int client = open_connection(&server);

    (void)client;
    TEST_ASSERT(server >= 0); /* the third SYN got through (after 1 s and 2 s) */
}

static void test_refused(void)
{
    setup();

    int client = tcp_connect(&stack_a, ip_b, 9999); /* nobody listens */

    run_ms(10);
    TEST_ASSERT_EQ_INT(TCP_CLOSED, tcp_state(&stack_a, client));
    TEST_ASSERT_EQ_INT(TCP_ERROR_REFUSED, tcp_error(&stack_a, client));

    char buffer[4];

    TEST_ASSERT_EQ_INT(-1, tcp_recv(&stack_a, client, buffer, sizeof(buffer)));
    tcp_abort(&stack_a, client);
}

static void test_reset(void)
{
    setup();

    int server;
    int client = open_connection(&server);
    char buffer[8];

    run_ms(5);
    tcp_abort(&stack_a, client); /* sends a RST */
    run_ms(5);
    TEST_ASSERT_EQ_INT(TCP_CLOSED, tcp_state(&stack_b, server));
    TEST_ASSERT_EQ_INT(TCP_ERROR_RESET, tcp_error(&stack_b, server));
    TEST_ASSERT_EQ_INT(-1, tcp_recv(&stack_b, server, buffer, sizeof(buffer)));
    tcp_abort(&stack_b, server);
}

static void test_flow_control(void)
{
    setup();

    int server;
    int client = open_connection(&server);
    static uint8_t block[TCP_BUFFER];
    size_t sent = 0;

    memset(block, 0x5A, sizeof(block));

    /* B reads nothing: A can only get TCP_BUFFER (B's window) plus its own TCP_BUFFER of send buffer out. */
    for (int i = 0; i < 3000; i++)
    {
        int n = tcp_send(&stack_a, client, block, sizeof(block));

        if (n > 0)
            sent += (size_t)n;

        step();
    }

    TEST_ASSERT(sent <= 2 * TCP_BUFFER);
    TEST_ASSERT(sent >= TCP_BUFFER);
    TEST_ASSERT_EQ_INT(TCP_BUFFER, tcp_receive_available(&stack_b, server));

    /* B reads everything: the window opens and the rest arrives. */
    size_t got = 0;
    static uint8_t sink[TCP_BUFFER];

    for (int i = 0; i < 6000 && got < sent; i++)
    {
        int n = tcp_recv(&stack_b, server, sink, sizeof(sink));

        if (n > 0)
            got += (size_t)n;

        step();
    }

    TEST_ASSERT_EQ_INT((int)sent, (int)got);
    tcp_abort(&stack_a, client);
    tcp_abort(&stack_b, server);
}

/* The window update that opens a closed window is lost: the sender must find out by probing. */
static void test_lost_window_update(void)
{
    setup();

    int server;
    int client = open_connection(&server);
    static uint8_t block[TCP_BUFFER];
    static uint8_t sink[TCP_BUFFER];
    size_t sent = 0;

    memset(block, 0x33, sizeof(block));

    /* Fill everything: B's buffer is full and its window is closed, A's send buffer too. */
    for (int i = 0; i < 3000; i++)
    {
        int n = tcp_send(&stack_a, client, block, sizeof(block));

        if (n > 0)
            sent += (size_t)n;

        step();
    }

    TEST_ASSERT_EQ_INT(TCP_BUFFER, tcp_receive_available(&stack_b, server));

    /* B reads it all; the ACK that says "window open" never arrives. */
    drop_next_to_a = 1;

    size_t got = 0;
    int n = tcp_recv(&stack_b, server, sink, sizeof(sink));

    TEST_ASSERT(n > 0);
    got += (size_t)n;

    /* Nothing else is lost; A has data waiting and has to ask. */
    for (int i = 0; i < 40000 && got < sent; i++)
    {
        n = tcp_recv(&stack_b, server, sink, sizeof(sink));

        if (n > 0)
            got += (size_t)n;

        step();
    }

    TEST_ASSERT_EQ_INT((int)sent, (int)got);
    tcp_abort(&stack_a, client);
    tcp_abort(&stack_b, server);
}

static void test_listener_close_drops_children(void)
{
    setup();

    int listener = tcp_listen(&stack_b, 80);
    int client = tcp_connect(&stack_a, ip_b, 80);

    run_ms(20);
    TEST_ASSERT(client >= 0);

    /* The connection is established but nobody accepts it; closing the listener frees its slot. */
    tcp_abort(&stack_b, listener);

    int used = 0;

    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
        used += stack_b.connection[i].used;

    TEST_ASSERT_EQ_INT(0, used);
    tcp_abort(&stack_a, client);
}

static void test_timeout(void)
{
    setup();

    int server;
    int client = open_connection(&server);

    run_ms(5);
    drop_all = 1;
    TEST_ASSERT_EQ_INT(4, tcp_send(&stack_a, client, "lost", 4));
    run_ms(60000);
    TEST_ASSERT_EQ_INT(TCP_CLOSED, tcp_state(&stack_a, client));
    TEST_ASSERT_EQ_INT(TCP_ERROR_TIMEOUT, tcp_error(&stack_a, client));
    tcp_abort(&stack_a, client);
    tcp_abort(&stack_b, server);
}

static void test_ports_and_slots(void)
{
    setup();
    TEST_ASSERT(tcp_listen(&stack_b, 80) >= 0);
    TEST_ASSERT_EQ_INT(-1, tcp_listen(&stack_b, 80)); /* the port is taken */

    int ids[TCP_MAX_CONNECTIONS];
    int count = 0;

    /* A has no listener: fill every slot with connections (they never complete). */
    drop_all = 1;

    while (count < TCP_MAX_CONNECTIONS && (ids[count] = tcp_connect(&stack_a, ip_b, 80)) >= 0)
        count++;

    TEST_ASSERT_EQ_INT(TCP_MAX_CONNECTIONS, count);
    TEST_ASSERT_EQ_INT(-1, tcp_connect(&stack_a, ip_b, 80));
    tcp_abort(&stack_a, ids[0]);
    TEST_ASSERT(tcp_connect(&stack_a, ip_b, 80) >= 0);
}

int main(void)
{
    TEST_RUN(test_handshake_and_data);
    TEST_RUN(test_large_transfer);
    TEST_RUN(test_loss_is_repaired);
    TEST_RUN(test_syn_is_retransmitted);
    TEST_RUN(test_refused);
    TEST_RUN(test_reset);
    TEST_RUN(test_flow_control);
    TEST_RUN(test_lost_window_update);
    TEST_RUN(test_listener_close_drops_children);
    TEST_RUN(test_timeout);
    TEST_RUN(test_ports_and_slots);

    return TEST_REPORT("tcp");
}

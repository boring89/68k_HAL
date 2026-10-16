/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_NET_TCP_H
#define KERNEL_NET_TCP_H

#include <stddef.h>
#include <stdint.h>

/*
 * TCP: a reliable stream of bytes over IP (RFC 793). A client opens a
 * connection with tcp_connect, a server waits with tcp_listen and
 * tcp_accept; then both sides tcp_send and tcp_recv until they tcp_close.
 *
 * What it does: the three-way handshake, a sliding window in both
 * directions, retransmission of lost data (timeout with exponential
 * back-off, go-back-N), orderly close (FIN) and reset (RST). What it does
 * not: out-of-order data is dropped and asked for again, no congestion
 * control, no selective acknowledgements, no window scaling (windows are
 * at most 16 KiB anyway), no urgent data, TIME_WAIT is short.
 *
 * The stack is plain data and functions: it sends segments through a
 * function it is given and reads the time from another, so it runs under
 * the host tests (two stacks wired together, with packet loss) exactly as
 * in the kernel. Nothing blocks; callers poll (see net.c, which also
 * provides blocking helpers).
 */

#define TCP_MAX_CONNECTIONS 6
#define TCP_BUFFER 16384 /* bytes per direction per connection */
#define TCP_MSS 1460

#define TCP_EOF (-2) /* tcp_recv: the peer closed and everything was read */

typedef enum
{
    TCP_CLOSED,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSING,
    TCP_TIME_WAIT,
    TCP_CLOSE_WAIT,
    TCP_LAST_ACK
} tcp_state_t;

typedef struct
{
    void *context;
    /* Send a TCP segment (header and data, checksum done) to destination_ip in an IP packet. Returns 0 or -1. */
    int (*send)(void *context, const uint8_t destination_ip[4], const uint8_t *segment, size_t length);
    /* Milliseconds since some fixed moment; must not go backwards. */
    uint64_t (*now_ms)(void *context);
    uint8_t local_ip[4];
} tcp_env_t;

typedef struct
{
    int used;   /* the slot belongs to a connection (until tcp_abort) */
    tcp_state_t state;
    uint8_t remote_ip[4];
    uint16_t remote_port;
    uint16_t local_port;
    int parent; /* a connection accepted by tcp_accept later: the listener, else -1 */

    uint32_t iss; /* our first sequence number */
    uint32_t snd_una; /* oldest byte not yet acknowledged */
    uint32_t snd_nxt; /* next byte to send */
    uint32_t irs;     /* the peer's first sequence number */
    uint32_t rcv_nxt; /* next byte expected from the peer */
    uint32_t snd_wnd; /* the window the peer offers */
    uint16_t mss;     /* the largest segment we send */

    uint8_t tx[TCP_BUFFER];
    uint32_t tx_head;   /* index in tx of the byte at snd_una */
    uint32_t tx_length; /* bytes held: acknowledged-not-yet plus unsent */
    uint32_t tx_sent;   /* of those, bytes already sent and not acknowledged */

    uint8_t rx[TCP_BUFFER];
    uint32_t rx_head;
    uint32_t rx_length;

    int fin_queued;  /* the program closed: send FIN after the data */
    int fin_sent;
    int peer_fin;    /* the peer's FIN has arrived */
    int error;       /* nonzero once reset or timed out (a K_E* number would be wrong layer: 1 = reset, 2 = timeout) */

    uint64_t rexmit_at; /* when to retransmit, 0 = no timer */
    uint32_t rto_ms;
    uint32_t retries;
    uint64_t time_wait_until;
    int probe;          /* send one byte into a zero window at the next timeout */
} tcp_connection_t;

typedef struct
{
    tcp_env_t env;
    tcp_connection_t connection[TCP_MAX_CONNECTIONS];
    uint16_t next_port;
    uint32_t iss_counter;
} tcp_stack_t;

#define TCP_ERROR_RESET 1
#define TCP_ERROR_TIMEOUT 2
#define TCP_ERROR_REFUSED 3

void tcp_init(tcp_stack_t *stack, const tcp_env_t *env);

/* Start a connection (sends the SYN). Returns the connection number, or -1 if there is no free slot. Poll tcp_state for TCP_ESTABLISHED. */
int tcp_connect(tcp_stack_t *stack, const uint8_t remote_ip[4], uint16_t remote_port);

/* Wait for connections on a port. Returns the listener's number, or -1 (port in use, no slot). */
int tcp_listen(tcp_stack_t *stack, uint16_t port);

/* A connection that has completed the handshake with this listener, or -1 if there is none yet. */
int tcp_accept(tcp_stack_t *stack, int listener);

/* Queue data to send. Returns how many bytes were taken (0 if the buffer is full), or -1 if the connection cannot send. */
int tcp_send(tcp_stack_t *stack, int connection, const void *data, size_t length);

/* Take received data. Returns the bytes copied, 0 if there is none now, TCP_EOF at the end of the stream, -1 on a reset or timeout. */
int tcp_recv(tcp_stack_t *stack, int connection, void *buffer, size_t length);

/* Say that we send no more (queues the FIN). The connection stays usable for receiving until the peer closes. */
void tcp_close(tcp_stack_t *stack, int connection);

/* Drop the connection at once (sends a RST if it was open) and free its slot. Always call it when you are done with a connection, whatever its state. */
void tcp_abort(tcp_stack_t *stack, int connection);

tcp_state_t tcp_state(const tcp_stack_t *stack, int connection);

/* The TCP_ERROR_* that ended the connection, 0 if none. */
int tcp_error(const tcp_stack_t *stack, int connection);

/* Bytes tcp_send would take now. */
size_t tcp_send_space(const tcp_stack_t *stack, int connection);

/* Bytes tcp_recv would return now. */
size_t tcp_receive_available(const tcp_stack_t *stack, int connection);

/* A segment arrived from source_ip (TCP header onwards). */
void tcp_input(tcp_stack_t *stack, const uint8_t source_ip[4], const uint8_t *segment, size_t length);

/* Let time pass: retransmissions and timeouts. Call often (every few ms). */
void tcp_tick(tcp_stack_t *stack);

#endif

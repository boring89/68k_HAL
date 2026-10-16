/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "tcp.h"

#include "packet.h"

#include "kernel/lib/k_memory.h"

/* Sequence numbers wrap around at 2^32: compare by the sign of the difference. */
#define SEQ_LT(a, b) ((int32_t)((a) - (b)) < 0)
#define SEQ_LE(a, b) ((int32_t)((a) - (b)) <= 0)

#define RTO_INITIAL_MS 1000u
#define RTO_MAX_MS 8000u
#define MAX_RETRIES 6u
#define TIME_WAIT_MS 2000u /* the RFC says two minutes; nobody here needs that */

static uint64_t now(const tcp_stack_t *stack)
{
    return stack->env.now_ms(stack->env.context);
}

static tcp_connection_t *get(tcp_stack_t *stack, int id)
{
    if (id < 0 || id >= TCP_MAX_CONNECTIONS || !stack->connection[id].used)
        return NULL;

    return &stack->connection[id];
}

static uint32_t free_receive_space(const tcp_connection_t *c)
{
    return TCP_BUFFER - c->rx_length;
}

/* Send one segment of connection c. */
static void send_segment(tcp_stack_t *stack, tcp_connection_t *c, uint8_t flags, uint32_t sequence,
                         const uint8_t *data, size_t length, int with_mss)
{
    static uint8_t buffer[TCP_MSS + 24];
    net_tcp_t tcp;
    uint32_t window = free_receive_space(c);

    tcp.source_port = c->local_port;
    tcp.destination_port = c->remote_port;
    tcp.sequence = sequence;
    tcp.acknowledge = c->rcv_nxt;
    tcp.flags = flags;
    tcp.window = window > 0xFFFF ? 0xFFFF : (uint16_t)window;
    tcp.mss = with_mss ? TCP_MSS : 0;
    tcp.data = data;
    tcp.data_length = length;

    size_t total = net_tcp_build(buffer, &tcp, stack->env.local_ip, c->remote_ip);

    stack->env.send(stack->env.context, c->remote_ip, buffer, total);
}

/* Answer a segment that belongs to no connection (RFC 793 3.4, "Reset Generation"). */
static void send_reset(tcp_stack_t *stack, const uint8_t remote_ip[4], const net_tcp_t *in)
{
    static uint8_t buffer[NET_TCP_HEADER];
    net_tcp_t tcp;

    if (in->flags & NET_TCP_RST)
        return;

    tcp.source_port = in->destination_port;
    tcp.destination_port = in->source_port;
    tcp.window = 0;
    tcp.mss = 0;
    tcp.data = NULL;
    tcp.data_length = 0;

    if (in->flags & NET_TCP_ACK)
    {
        tcp.sequence = in->acknowledge;
        tcp.acknowledge = 0;
        tcp.flags = NET_TCP_RST;
    }
    else
    {
        uint32_t length = (uint32_t)in->data_length + ((in->flags & NET_TCP_SYN) ? 1 : 0) +
                          ((in->flags & NET_TCP_FIN) ? 1 : 0);

        tcp.sequence = 0;
        tcp.acknowledge = in->sequence + length;
        tcp.flags = NET_TCP_RST | NET_TCP_ACK;
    }

    size_t total = net_tcp_build(buffer, &tcp, stack->env.local_ip, remote_ip);

    stack->env.send(stack->env.context, remote_ip, buffer, total);
}

static void start_timer(tcp_stack_t *stack, tcp_connection_t *c)
{
    if (c->rexmit_at == 0)
        c->rexmit_at = now(stack) + c->rto_ms;
}

static void set_closed(tcp_connection_t *c, int error)
{
    c->state = TCP_CLOSED;
    c->rexmit_at = 0;

    if (error != 0)
        c->error = error;
}

/* Send what the window allows, then the FIN if the program has closed. */
static void output(tcp_stack_t *stack, tcp_connection_t *c)
{
    static uint8_t chunk[TCP_MSS];

    if (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT && c->state != TCP_FIN_WAIT_1 &&
        c->state != TCP_LAST_ACK && c->state != TCP_CLOSING)
        return;

    for (;;)
    {
        uint32_t unsent = c->tx_length - c->tx_sent;
        uint32_t room = c->snd_wnd > c->tx_sent ? c->snd_wnd - c->tx_sent : 0;

        /* A zero window: one byte now and then asks whether it has opened (a window probe). */
        if (room == 0 && unsent > 0 && c->tx_sent == 0 && c->probe)
        {
            room = 1;
            c->probe = 0;
        }

        uint32_t n = unsent < room ? unsent : room;

        if (n > c->mss)
            n = c->mss;

        if (n == 0)
            break;

        for (uint32_t i = 0; i < n; i++)
            chunk[i] = c->tx[(c->tx_head + c->tx_sent + i) % TCP_BUFFER];

        send_segment(stack, c, NET_TCP_ACK | (n == unsent ? NET_TCP_PSH : 0), c->snd_nxt, chunk, n, 0);
        c->snd_nxt += n;
        c->tx_sent += n;
        start_timer(stack, c);
    }

    if (c->fin_queued && !c->fin_sent && c->tx_sent == c->tx_length)
    {
        send_segment(stack, c, NET_TCP_FIN | NET_TCP_ACK, c->snd_nxt, NULL, 0, 0);
        c->snd_nxt++;
        c->fin_sent = 1;
        start_timer(stack, c);

        if (c->state == TCP_ESTABLISHED)
            c->state = TCP_FIN_WAIT_1;
        else if (c->state == TCP_CLOSE_WAIT)
            c->state = TCP_LAST_ACK;
    }
}

void tcp_init(tcp_stack_t *stack, const tcp_env_t *env)
{
    k_memset(stack, 0, sizeof(*stack));
    stack->env = *env;
    stack->next_port = 49152;
    stack->iss_counter = (uint32_t)now(stack) * 64000u + 12345u;
}

static tcp_connection_t *allocate(tcp_stack_t *stack, int *id)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
    {
        if (!stack->connection[i].used)
        {
            tcp_connection_t *c = &stack->connection[i];

            k_memset(c, 0, sizeof(*c));
            c->used = 1;
            c->parent = -1;
            c->rto_ms = RTO_INITIAL_MS;
            c->mss = 536; /* the default until the peer's SYN says more (RFC 879) */
            *id = i;

            return c;
        }
    }

    return NULL;
}

static uint32_t next_iss(tcp_stack_t *stack)
{
    stack->iss_counter += 64000u + (uint32_t)now(stack);

    return stack->iss_counter;
}

int tcp_connect(tcp_stack_t *stack, const uint8_t remote_ip[4], uint16_t remote_port)
{
    int id;
    tcp_connection_t *c = allocate(stack, &id);

    if (c == NULL)
        return -1;

    k_memcpy(c->remote_ip, remote_ip, 4);
    c->remote_port = remote_port;
    c->local_port = stack->next_port++;

    if (stack->next_port < 49152)
        stack->next_port = 49152;

    c->iss = next_iss(stack);
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1;
    c->state = TCP_SYN_SENT;

    send_segment(stack, c, NET_TCP_SYN, c->iss, NULL, 0, 1);
    start_timer(stack, c);

    return id;
}

int tcp_listen(tcp_stack_t *stack, uint16_t port)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
    {
        const tcp_connection_t *c = &stack->connection[i];

        if (c->used && c->state == TCP_LISTEN && c->local_port == port)
            return -1;
    }

    int id;
    tcp_connection_t *c = allocate(stack, &id);

    if (c == NULL)
        return -1;

    c->local_port = port;
    c->state = TCP_LISTEN;

    return id;
}

int tcp_accept(tcp_stack_t *stack, int listener)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
    {
        tcp_connection_t *c = &stack->connection[i];

        if (c->used && c->parent == listener && c->state != TCP_SYN_RECEIVED)
        {
            c->parent = -1;

            return i;
        }
    }

    return -1;
}

int tcp_send(tcp_stack_t *stack, int id, const void *data, size_t length)
{
    tcp_connection_t *c = get(stack, id);

    if (c == NULL || c->fin_queued || (c->state != TCP_ESTABLISHED && c->state != TCP_CLOSE_WAIT))
        return -1;

    size_t space = TCP_BUFFER - c->tx_length;

    if (length > space)
        length = space;

    const uint8_t *bytes = data;

    for (size_t i = 0; i < length; i++)
        c->tx[(c->tx_head + c->tx_length + i) % TCP_BUFFER] = bytes[i];

    c->tx_length += (uint32_t)length;
    output(stack, c);

    return (int)length;
}

int tcp_recv(tcp_stack_t *stack, int id, void *buffer, size_t length)
{
    tcp_connection_t *c = get(stack, id);

    if (c == NULL)
        return -1;

    if (c->rx_length == 0)
    {
        if (c->error)
            return -1;

        return c->peer_fin ? TCP_EOF : 0;
    }

    if (length > c->rx_length)
        length = c->rx_length;

    uint32_t space_before = free_receive_space(c);
    uint8_t *bytes = buffer;

    for (size_t i = 0; i < length; i++)
        bytes[i] = c->rx[(c->rx_head + i) % TCP_BUFFER];

    c->rx_head = (c->rx_head + (uint32_t)length) % TCP_BUFFER;
    c->rx_length -= (uint32_t)length;

    /* The window was almost closed and has opened: tell the peer, or it may wait forever. */
    if (space_before < c->mss && free_receive_space(c) >= c->mss &&
        (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1 || c->state == TCP_FIN_WAIT_2))
        send_segment(stack, c, NET_TCP_ACK, c->snd_nxt, NULL, 0, 0);

    return (int)length;
}

void tcp_close(tcp_stack_t *stack, int id)
{
    tcp_connection_t *c = get(stack, id);

    if (c == NULL)
        return;

    if (c->state == TCP_LISTEN || c->state == TCP_SYN_SENT)
    {
        set_closed(c, 0);

        return;
    }

    c->fin_queued = 1;
    output(stack, c);
}

void tcp_abort(tcp_stack_t *stack, int id)
{
    tcp_connection_t *c = get(stack, id);

    if (c == NULL)
        return;

    if (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1 || c->state == TCP_FIN_WAIT_2 ||
        c->state == TCP_CLOSE_WAIT || c->state == TCP_SYN_RECEIVED)
        send_segment(stack, c, NET_TCP_RST | NET_TCP_ACK, c->snd_nxt, NULL, 0, 0);

    c->used = 0;
    c->state = TCP_CLOSED;
}

tcp_state_t tcp_state(const tcp_stack_t *stack, int id)
{
    if (id < 0 || id >= TCP_MAX_CONNECTIONS || !stack->connection[id].used)
        return TCP_CLOSED;

    return stack->connection[id].state;
}

int tcp_error(const tcp_stack_t *stack, int id)
{
    if (id < 0 || id >= TCP_MAX_CONNECTIONS || !stack->connection[id].used)
        return 0;

    return stack->connection[id].error;
}

size_t tcp_send_space(const tcp_stack_t *stack, int id)
{
    if (id < 0 || id >= TCP_MAX_CONNECTIONS || !stack->connection[id].used)
        return 0;

    return TCP_BUFFER - stack->connection[id].tx_length;
}

size_t tcp_receive_available(const tcp_stack_t *stack, int id)
{
    if (id < 0 || id >= TCP_MAX_CONNECTIONS || !stack->connection[id].used)
        return 0;

    return stack->connection[id].rx_length;
}

/* --- input --- */

static tcp_connection_t *find(tcp_stack_t *stack, const uint8_t source_ip[4], const net_tcp_t *in)
{
    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
    {
        tcp_connection_t *c = &stack->connection[i];

        if (c->used && c->state != TCP_LISTEN && c->state != TCP_CLOSED &&
            c->local_port == in->destination_port && c->remote_port == in->source_port &&
            k_memcmp(c->remote_ip, source_ip, 4) == 0)
            return c;
    }

    return NULL;
}

static void handle_listen(tcp_stack_t *stack, const uint8_t source_ip[4], const net_tcp_t *in)
{
    int listener = -1;

    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
    {
        const tcp_connection_t *c = &stack->connection[i];

        if (c->used && c->state == TCP_LISTEN && c->local_port == in->destination_port)
            listener = i;
    }

    if (listener < 0 || !(in->flags & NET_TCP_SYN) || (in->flags & (NET_TCP_ACK | NET_TCP_RST)))
    {
        send_reset(stack, source_ip, in);

        return;
    }

    int id;
    tcp_connection_t *c = allocate(stack, &id);

    if (c == NULL)
        return; /* no room: the SYN is dropped and the peer tries again */

    k_memcpy(c->remote_ip, source_ip, 4);
    c->remote_port = in->source_port;
    c->local_port = in->destination_port;
    c->parent = listener;
    c->irs = in->sequence;
    c->rcv_nxt = in->sequence + 1;
    c->iss = next_iss(stack);
    c->snd_una = c->iss;
    c->snd_nxt = c->iss + 1;
    c->snd_wnd = in->window;
    c->mss = in->mss != 0 && in->mss < TCP_MSS ? in->mss : TCP_MSS;
    c->state = TCP_SYN_RECEIVED;

    send_segment(stack, c, NET_TCP_SYN | NET_TCP_ACK, c->iss, NULL, 0, 1);
    start_timer(stack, c);
}

static void handle_syn_sent(tcp_stack_t *stack, tcp_connection_t *c, const net_tcp_t *in)
{
    int ack_ok = (in->flags & NET_TCP_ACK) && in->acknowledge == c->snd_nxt;

    if ((in->flags & NET_TCP_ACK) && !ack_ok)
    {
        static uint8_t buffer[NET_TCP_HEADER];
        net_tcp_t rst = {in->destination_port, in->source_port, in->acknowledge, 0, NET_TCP_RST, 0, 0, NULL, 0};

        if (!(in->flags & NET_TCP_RST))
        {
            size_t total = net_tcp_build(buffer, &rst, stack->env.local_ip, c->remote_ip);

            stack->env.send(stack->env.context, c->remote_ip, buffer, total);
        }

        return;
    }

    if (in->flags & NET_TCP_RST)
    {
        if (ack_ok)
            set_closed(c, TCP_ERROR_REFUSED);

        return;
    }

    if (!(in->flags & NET_TCP_SYN))
        return;

    c->irs = in->sequence;
    c->rcv_nxt = in->sequence + 1;
    c->snd_wnd = in->window;
    c->mss = in->mss != 0 && in->mss < TCP_MSS ? in->mss : (in->mss != 0 ? TCP_MSS : 536);

    if (ack_ok)
    {
        c->snd_una = in->acknowledge;
        c->state = TCP_ESTABLISHED;
        c->rexmit_at = 0;
        c->retries = 0;
        c->rto_ms = RTO_INITIAL_MS;
        send_segment(stack, c, NET_TCP_ACK, c->snd_nxt, NULL, 0, 0);
    }
    else
    {
        /* A simultaneous open: both sides sent a SYN. */
        c->state = TCP_SYN_RECEIVED;
        send_segment(stack, c, NET_TCP_SYN | NET_TCP_ACK, c->iss, NULL, 0, 1);
    }
}

/* Process the acknowledgement field. */
static void handle_ack(tcp_stack_t *stack, tcp_connection_t *c, const net_tcp_t *in)
{
    if (!(in->flags & NET_TCP_ACK))
        return;

    if (c->state == TCP_SYN_RECEIVED)
    {
        if (in->acknowledge != c->iss + 1)
            return;

        c->snd_una = in->acknowledge;
        c->state = TCP_ESTABLISHED;
        c->rexmit_at = 0;
        c->retries = 0;
        c->snd_wnd = in->window;

        return;
    }

    if (SEQ_LT(c->snd_una, in->acknowledge) && SEQ_LE(in->acknowledge, c->snd_nxt))
    {
        uint32_t acked = in->acknowledge - c->snd_una;
        int fin_acked = c->fin_sent && in->acknowledge == c->snd_nxt;

        if (fin_acked)
            acked--;

        if (acked > c->tx_sent)
            acked = c->tx_sent;

        c->tx_head = (c->tx_head + acked) % TCP_BUFFER;
        c->tx_length -= acked;
        c->tx_sent -= acked;
        c->snd_una = in->acknowledge;
        c->retries = 0;
        c->rto_ms = RTO_INITIAL_MS;
        c->rexmit_at = 0;

        if (c->snd_una != c->snd_nxt)
            start_timer(stack, c);

        if (fin_acked)
        {
            if (c->state == TCP_FIN_WAIT_1)
                c->state = TCP_FIN_WAIT_2;
            else if (c->state == TCP_CLOSING)
            {
                c->state = TCP_TIME_WAIT;
                c->time_wait_until = now(stack) + TIME_WAIT_MS;
            }
            else if (c->state == TCP_LAST_ACK)
                set_closed(c, 0);
        }
    }

    if (SEQ_LE(c->snd_una, in->acknowledge))
    {
        c->snd_wnd = in->window;

        if (c->snd_wnd > 0)
            c->probe = 0;
    }
}

void tcp_input(tcp_stack_t *stack, const uint8_t source_ip[4], const uint8_t *segment, size_t length)
{
    net_tcp_t in;

    if (net_tcp_parse(segment, length, source_ip, stack->env.local_ip, &in) != 0)
        return;

    tcp_connection_t *c = find(stack, source_ip, &in);

    if (c == NULL)
    {
        handle_listen(stack, source_ip, &in);

        return;
    }

    if (c->state == TCP_SYN_SENT)
    {
        handle_syn_sent(stack, c, &in);
        output(stack, c);

        return;
    }

    /* A reset ends the connection (it must be inside the window, RFC 793 3.4; here: not before what we expect). */
    if (in.flags & NET_TCP_RST)
    {
        if (SEQ_LE(c->rcv_nxt, in.sequence) || c->state == TCP_SYN_RECEIVED)
            set_closed(c, TCP_ERROR_RESET);

        return;
    }

    /* A SYN again: the peer did not get our SYN|ACK. */
    if ((in.flags & NET_TCP_SYN) && c->state == TCP_SYN_RECEIVED)
    {
        send_segment(stack, c, NET_TCP_SYN | NET_TCP_ACK, c->iss, NULL, 0, 1);

        return;
    }

    handle_ack(stack, c, &in);

    int need_ack = 0;

    /* Data: only what continues exactly where we are; the overlap of a retransmission is skipped. */
    if (in.data_length > 0 &&
        (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1 || c->state == TCP_FIN_WAIT_2))
    {
        if (SEQ_LE(in.sequence, c->rcv_nxt) && SEQ_LT(c->rcv_nxt, in.sequence + (uint32_t)in.data_length))
        {
            uint32_t skip = c->rcv_nxt - in.sequence;
            uint32_t take = (uint32_t)in.data_length - skip;
            uint32_t space = free_receive_space(c);

            if (take > space)
                take = space;

            for (uint32_t i = 0; i < take; i++)
                c->rx[(c->rx_head + c->rx_length + i) % TCP_BUFFER] = in.data[skip + i];

            c->rx_length += take;
            c->rcv_nxt += take;
        }

        need_ack = 1; /* also for a gap or a duplicate: the ACK says what we are waiting for */
    }

    /* The FIN counts once all the data before it has arrived. */
    if ((in.flags & NET_TCP_FIN) &&
        (c->state == TCP_ESTABLISHED || c->state == TCP_FIN_WAIT_1 || c->state == TCP_FIN_WAIT_2 ||
         c->state == TCP_CLOSE_WAIT || c->state == TCP_CLOSING || c->state == TCP_LAST_ACK ||
         c->state == TCP_TIME_WAIT))
    {
        uint32_t fin_sequence = in.sequence + (uint32_t)in.data_length;

        if (!c->peer_fin && fin_sequence == c->rcv_nxt)
        {
            c->rcv_nxt++;
            c->peer_fin = 1;

            if (c->state == TCP_ESTABLISHED)
                c->state = TCP_CLOSE_WAIT;
            else if (c->state == TCP_FIN_WAIT_1)
                c->state = TCP_CLOSING;
            else if (c->state == TCP_FIN_WAIT_2)
            {
                c->state = TCP_TIME_WAIT;
                c->time_wait_until = now(stack) + TIME_WAIT_MS;
            }
        }

        need_ack = 1;
    }

    if (need_ack)
        send_segment(stack, c, NET_TCP_ACK, c->snd_nxt, NULL, 0, 0);

    output(stack, c);
}

void tcp_tick(tcp_stack_t *stack)
{
    uint64_t t = now(stack);

    for (int i = 0; i < TCP_MAX_CONNECTIONS; i++)
    {
        tcp_connection_t *c = &stack->connection[i];

        if (!c->used)
            continue;

        if (c->state == TCP_TIME_WAIT && t >= c->time_wait_until)
        {
            set_closed(c, 0);
            continue;
        }

        if (c->rexmit_at == 0 || t < c->rexmit_at)
            continue;

        if (++c->retries > MAX_RETRIES)
        {
            set_closed(c, TCP_ERROR_TIMEOUT);
            continue;
        }

        c->rto_ms = c->rto_ms * 2 > RTO_MAX_MS ? RTO_MAX_MS : c->rto_ms * 2;
        c->rexmit_at = t + c->rto_ms;

        if (c->state == TCP_SYN_SENT)
        {
            send_segment(stack, c, NET_TCP_SYN, c->iss, NULL, 0, 1);
        }
        else if (c->state == TCP_SYN_RECEIVED)
        {
            send_segment(stack, c, NET_TCP_SYN | NET_TCP_ACK, c->iss, NULL, 0, 1);
        }
        else
        {
            /* Go back to the oldest unacknowledged byte and send everything again. */
            c->snd_nxt = c->snd_una;
            c->tx_sent = 0;
            c->fin_sent = 0;

            if (c->tx_length > 0 && c->snd_wnd == 0)
                c->probe = 1;

            output(stack, c);
        }
    }
}

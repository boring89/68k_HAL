/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "net.h"

#include "packet.h"
#include "tcp.h"

#include "hal/cpu.h"
#include "hal/timer.h"

#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_number.h"
#include "kernel/lib/k_string.h"

static net_device_t *device;
static net_config_t config;
static tcp_stack_t tcp_stack;
static int tcp_ready;

static void tcp_restart(void);

static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t broadcast_ip[4] = {255, 255, 255, 255};
static const uint8_t zero_ip[4] = {0, 0, 0, 0};

static uint16_t ip_id;

/* --- small helpers --- */

static int same_ip(const uint8_t a[4], const uint8_t b[4])
{
    return k_memcmp(a, b, 4) == 0;
}

static int is_zero_ip(const uint8_t a[4])
{
    return same_ip(a, zero_ip);
}

/* Run net_poll until done() is true or timeout_ms has passed. Interrupts must be on. */
static int wait_for(volatile int *flag, uint32_t timeout_ms)
{
    uint64_t end = hal_timer_ns() + (uint64_t)timeout_ms * 1000000u;

    while (!*flag)
    {
        net_poll();

        if (*flag)
            break;

        if (hal_timer_ns() >= end)
            return -1;

        hal_cpu_idle();
    }

    return 0;
}

void net_format_ip(const uint8_t ip[4], char *text)
{
    for (int i = 0; i < 4; i++)
    {
        char number[12];

        k_utoa(ip[i], number);
        k_strcpy(text, number);
        text += k_strlen(number);

        if (i < 3)
            *text++ = '.';
    }

    *text = '\0';
}

/* --- device --- */

int net_register(net_device_t *dev)
{
    if (device != NULL)
        return -1;

    device = dev;

    return 0;
}

net_device_t *net_device_get(void)
{
    return device;
}

void net_poll(void)
{
    if (device != NULL && device->poll != NULL)
        device->poll(device);

    if (tcp_ready)
        tcp_tick(&tcp_stack);
}

const net_config_t *net_config(void)
{
    return &config;
}

void net_configure(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gateway[4], const uint8_t dns[4])
{
    k_memcpy(config.ip, ip, 4);
    k_memcpy(config.mask, mask, 4);
    k_memcpy(config.gateway, gateway, 4);
    k_memcpy(config.dns, dns, 4);
    config.configured = !is_zero_ip(ip);

    if (config.configured)
        tcp_restart();
}

/* --- ARP --- */

#define ARP_ENTRIES 8

typedef struct
{
    int valid;
    uint8_t ip[4];
    uint8_t mac[6];
} arp_entry_t;

static arp_entry_t arp_cache[ARP_ENTRIES];
static unsigned int arp_next;

static const uint8_t *arp_lookup(const uint8_t ip[4])
{
    for (int i = 0; i < ARP_ENTRIES; i++)
    {
        if (arp_cache[i].valid && same_ip(arp_cache[i].ip, ip))
            return arp_cache[i].mac;
    }

    return NULL;
}

static void arp_learn(const uint8_t ip[4], const uint8_t mac[6])
{
    for (int i = 0; i < ARP_ENTRIES; i++)
    {
        if (arp_cache[i].valid && same_ip(arp_cache[i].ip, ip))
        {
            k_memcpy(arp_cache[i].mac, mac, 6);

            return;
        }
    }

    arp_entry_t *entry = &arp_cache[arp_next++ % ARP_ENTRIES];

    entry->valid = 1;
    k_memcpy(entry->ip, ip, 4);
    k_memcpy(entry->mac, mac, 6);
}

static int send_frame(const uint8_t *frame, size_t length)
{
    if (device == NULL)
        return -1;

    return device->send(device, frame, (uint32_t)length);
}

static void send_arp(uint16_t operation, const uint8_t target_mac[6], const uint8_t target_ip[4])
{
    uint8_t frame[NET_ETH_HEADER + NET_ARP_PACKET];
    net_arp_t arp;

    arp.operation = operation;
    k_memcpy(arp.sender_mac, device->mac, 6);
    k_memcpy(arp.sender_ip, config.ip, 4);
    k_memset(arp.target_mac, 0, 6);

    if (operation == NET_ARP_REPLY)
        k_memcpy(arp.target_mac, target_mac, 6);

    k_memcpy(arp.target_ip, target_ip, 4);

    net_eth_build(frame, operation == NET_ARP_REQUEST ? broadcast_mac : target_mac,
                  device->mac, NET_ETHERTYPE_ARP);
    net_arp_build(frame + NET_ETH_HEADER, &arp);
    send_frame(frame, sizeof(frame));
}

static void handle_arp(const uint8_t *data, size_t length)
{
    net_arp_t arp;

    if (net_arp_parse(data, length, &arp) != 0)
        return;

    if (!is_zero_ip(arp.sender_ip))
        arp_learn(arp.sender_ip, arp.sender_mac);

    if (arp.operation == NET_ARP_REQUEST && config.configured && same_ip(arp.target_ip, config.ip))
        send_arp(NET_ARP_REPLY, arp.sender_mac, arp.sender_ip);
}

/* Find the MAC address to send to for ip: on the subnet directly, else the gateway. */
static int resolve_mac(const uint8_t ip[4], uint8_t mac[6])
{
    uint8_t next_hop[4];
    int local = 1;

    for (int i = 0; i < 4; i++)
    {
        if ((ip[i] & config.mask[i]) != (config.ip[i] & config.mask[i]))
            local = 0;
    }

    k_memcpy(next_hop, local ? ip : config.gateway, 4);

    for (int attempt = 0; attempt < 3; attempt++)
    {
        const uint8_t *known = arp_lookup(next_hop);

        if (known != NULL)
        {
            k_memcpy(mac, known, 6);

            return 0;
        }

        send_arp(NET_ARP_REQUEST, broadcast_mac, next_hop);

        uint64_t end = hal_timer_ns() + 500000000u;

        while (arp_lookup(next_hop) == NULL && hal_timer_ns() < end)
        {
            net_poll();
            hal_cpu_idle();
        }
    }

    return -1;
}

/* --- IPv4 sending --- */

/* Send an IP packet with the given payload. dest_mac NULL = resolve with ARP. */
static int send_ip(const uint8_t destination[4], const uint8_t *dest_mac, uint8_t protocol,
                   const uint8_t *payload, size_t length)
{
    static uint8_t frame[NET_FRAME_MAX];

    if (device == NULL || length > NET_MTU - NET_IP_HEADER)
        return -1;

    uint8_t mac[6];

    if (dest_mac != NULL)
        k_memcpy(mac, dest_mac, 6);
    else if (resolve_mac(destination, mac) != 0)
        return -1;

    net_eth_build(frame, mac, device->mac, NET_ETHERTYPE_IPV4);
    net_ip_build(frame + NET_ETH_HEADER, protocol, config.ip, destination, length, ip_id++);
    k_memcpy(frame + NET_ETH_HEADER + NET_IP_HEADER, payload, length);

    return send_frame(frame, NET_ETH_HEADER + NET_IP_HEADER + length);
}

/* --- UDP --- */

#define MAX_LISTENERS 6

typedef struct
{
    int used;
    uint16_t port;
    net_udp_handler_t handler;
    void *context;
} listener_t;

static listener_t listeners[MAX_LISTENERS];

int net_udp_listen(uint16_t port, net_udp_handler_t handler, void *context)
{
    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        if (listeners[i].used && listeners[i].port == port)
            return -1;
    }

    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        if (!listeners[i].used)
        {
            listeners[i].used = 1;
            listeners[i].port = port;
            listeners[i].handler = handler;
            listeners[i].context = context;

            return 0;
        }
    }

    return -1;
}

void net_udp_close(uint16_t port)
{
    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        if (listeners[i].used && listeners[i].port == port)
            listeners[i].used = 0;
    }
}

static int send_udp(const uint8_t destination[4], const uint8_t *dest_mac, uint16_t destination_port,
                    uint16_t source_port, const void *data, size_t length)
{
    static uint8_t datagram[NET_MTU];

    if (length + NET_UDP_HEADER > sizeof(datagram))
        return -1;

    size_t total = net_udp_build(datagram, source_port, destination_port, data, length,
                                 config.ip, destination);

    return send_ip(destination, dest_mac, NET_IP_PROTO_UDP, datagram, total);
}

int net_udp_send(const uint8_t destination_ip[4], uint16_t destination_port,
                 uint16_t source_port, const void *data, size_t length)
{
    return send_udp(destination_ip, NULL, destination_port, source_port, data, length);
}

static void handle_udp(const net_ip_t *ip)
{
    net_udp_t udp;

    if (net_udp_parse(ip->payload, ip->payload_length, ip->source, ip->destination, &udp) != 0)
        return;

    for (int i = 0; i < MAX_LISTENERS; i++)
    {
        if (listeners[i].used && listeners[i].port == udp.destination_port)
        {
            listeners[i].handler(ip->source, udp.source_port, udp.data, udp.data_length,
                                 listeners[i].context);

            return;
        }
    }
}

/* --- ICMP --- */

#define PING_ID 0x6848

static volatile int ping_answered;
static uint16_t ping_sequence;
static uint8_t ping_target[4];

static void handle_icmp(const net_ip_t *ip, const uint8_t *mac)
{
    net_icmp_echo_t echo;

    if (net_icmp_echo_parse(ip->payload, ip->payload_length, &echo) != 0)
        return;

    if (echo.type == NET_ICMP_ECHO_REQUEST)
    {
        uint8_t reply[NET_MTU];

        if (echo.data_length > sizeof(reply) - 8 - NET_IP_HEADER)
            return;

        size_t length = net_icmp_echo_build(reply, NET_ICMP_ECHO_REPLY, echo.id, echo.sequence,
                                            echo.data, echo.data_length);

        send_ip(ip->source, mac, NET_IP_PROTO_ICMP, reply, length);
    }
    else if (echo.id == PING_ID && echo.sequence == ping_sequence && same_ip(ip->source, ping_target))
    {
        ping_answered = 1;
    }
}

int net_ping(const uint8_t ip[4], uint32_t timeout_ms, uint32_t *round_trip_us)
{
    uint8_t request[8 + 32];
    uint8_t payload[32];

    if (device == NULL || !config.configured)
        return -1;

    for (int i = 0; i < 32; i++)
        payload[i] = (uint8_t)('a' + i % 26);

    size_t length = net_icmp_echo_build(request, NET_ICMP_ECHO_REQUEST, PING_ID,
                                        ++ping_sequence, payload, sizeof(payload));

    k_memcpy(ping_target, ip, 4);
    ping_answered = 0;

    uint64_t start = hal_timer_ns();

    if (send_ip(ip, NULL, NET_IP_PROTO_ICMP, request, length) != 0)
        return -1;

    if (wait_for(&ping_answered, timeout_ms) != 0)
        return -1;

    if (round_trip_us != NULL)
        *round_trip_us = (uint32_t)((hal_timer_ns() - start) / 1000);

    return 0;
}

/* --- TCP --- */

static int tcp_send_ip(void *context, const uint8_t destination_ip[4], const uint8_t *segment, size_t length)
{
    (void)context;

    return send_ip(destination_ip, NULL, NET_IP_PROTO_TCP, segment, length);
}

static uint64_t tcp_now_ms(void *context)
{
    (void)context;

    return hal_timer_ns() / 1000000u;
}

/* The TCP stack knows our address from the configuration: (re)start it when that changes. */
static void tcp_restart(void)
{
    tcp_env_t env;

    env.context = NULL;
    env.send = tcp_send_ip;
    env.now_ms = tcp_now_ms;
    k_memcpy(env.local_ip, config.ip, 4);
    tcp_init(&tcp_stack, &env);
    tcp_ready = 1;
}

tcp_stack_t *net_tcp(void)
{
    return &tcp_stack;
}

/* Poll the network and idle until done() says so or timeout_ms passes. Returns 0, or -1 on timeout. */
static int wait_until(int (*done)(void *context), void *context, uint32_t timeout_ms)
{
    uint64_t end = hal_timer_ns() + (uint64_t)timeout_ms * 1000000u;

    for (;;)
    {
        net_poll();

        if (done(context))
            return 0;

        if (hal_timer_ns() >= end)
            return -1;

        hal_cpu_idle();
    }
}

typedef struct
{
    int connection;
    const uint8_t *data;
    size_t length;
    size_t sent;
} tcp_wait_t;

static int connected(void *context)
{
    int connection = *(int *)context;
    tcp_state_t state = tcp_state(&tcp_stack, connection);

    return state != TCP_SYN_SENT && state != TCP_SYN_RECEIVED;
}

int net_tcp_connect(const uint8_t ip[4], uint16_t port, uint32_t timeout_ms)
{
    if (device == NULL || !config.configured || !tcp_ready)
        return -1;

    int connection = tcp_connect(&tcp_stack, ip, port);

    if (connection < 0)
        return -1;

    if (wait_until(connected, &connection, timeout_ms) != 0 ||
        tcp_state(&tcp_stack, connection) != TCP_ESTABLISHED)
    {
        tcp_abort(&tcp_stack, connection);

        return -1;
    }

    return connection;
}

static int sent_all(void *context)
{
    tcp_wait_t *w = context;

    if (w->sent < w->length)
    {
        int n = tcp_send(&tcp_stack, w->connection, w->data + w->sent, w->length - w->sent);

        if (n < 0)
            return 1; /* failed: stop waiting, the caller sees sent < length */

        w->sent += (size_t)n;
    }

    return w->sent >= w->length;
}

int net_tcp_send_all(int connection, const void *data, size_t length, uint32_t timeout_ms)
{
    tcp_wait_t w = {connection, data, length, 0};

    if (wait_until(sent_all, &w, timeout_ms) != 0)
        return -1;

    return w.sent == length ? 0 : -1;
}

typedef struct
{
    int connection;
    void *buffer;
    size_t length;
    int result;
} tcp_recv_wait_t;

static int received(void *context)
{
    tcp_recv_wait_t *w = context;

    w->result = tcp_recv(&tcp_stack, w->connection, w->buffer, w->length);

    return w->result != 0;
}

int net_tcp_recv(int connection, void *buffer, size_t length, uint32_t timeout_ms)
{
    tcp_recv_wait_t w = {connection, buffer, length, 0};

    if (wait_until(received, &w, timeout_ms) != 0)
        return -1;

    return w.result;
}

static int closed(void *context)
{
    tcp_state_t state = tcp_state(&tcp_stack, *(int *)context);

    return state == TCP_CLOSED || state == TCP_TIME_WAIT;
}

void net_tcp_close(int connection)
{
    tcp_close(&tcp_stack, connection);
    wait_until(closed, &connection, 2000);
    tcp_abort(&tcp_stack, connection);
}

/* --- receive --- */

void net_receive(net_device_t *dev, const uint8_t *frame, uint32_t length)
{
    (void)dev;

    if (length < NET_ETH_HEADER)
        return;

    uint16_t type = (uint16_t)((frame[12] << 8) | frame[13]);
    const uint8_t *data = frame + NET_ETH_HEADER;
    size_t data_length = length - NET_ETH_HEADER;

    if (type == NET_ETHERTYPE_ARP)
    {
        handle_arp(data, data_length);

        return;
    }

    if (type != NET_ETHERTYPE_IPV4)
        return;

    net_ip_t ip;

    if (net_ip_parse(data, data_length, &ip) != 0)
        return;

    /* Only our own address and broadcasts; before DHCP everything addressed to us by MAC is fine. */
    int for_us = same_ip(ip.destination, config.ip) || same_ip(ip.destination, broadcast_ip) ||
                 !config.configured;

    if (!for_us)
        return;

    if (ip.protocol == NET_IP_PROTO_TCP)
    {
        if (tcp_ready)
            tcp_input(&tcp_stack, ip.source, ip.payload, ip.payload_length);
    }
    else if (ip.protocol == NET_IP_PROTO_ICMP)
        handle_icmp(&ip, frame + 6);
    else if (ip.protocol == NET_IP_PROTO_UDP)
        handle_udp(&ip);
}

/* --- DHCP client --- */

static volatile int dhcp_state; /* 0 waiting, 1 got offer, 2 got ack */
static net_dhcp_t dhcp_reply;
static uint32_t dhcp_xid;

static void dhcp_handler(const uint8_t source_ip[4], uint16_t source_port,
                         const uint8_t *data, size_t length, void *context)
{
    net_dhcp_t reply;

    (void)source_ip;
    (void)source_port;
    (void)context;

    if (net_dhcp_parse(data, length, &reply) != 0 || reply.xid != dhcp_xid)
        return;

    if (dhcp_state == 0 && reply.type == NET_DHCP_OFFER)
    {
        dhcp_reply = reply;
        dhcp_state = 1;
    }
    else if (dhcp_state == 1 && reply.type == NET_DHCP_ACK)
    {
        dhcp_reply = reply;
        dhcp_state = 2;
    }
}

static int dhcp_send(uint8_t type)
{
    uint8_t message[320];
    size_t length = net_dhcp_build(message, type, device->mac, dhcp_xid, dhcp_reply.your_ip,
                                   dhcp_reply.server);

    return send_udp(broadcast_ip, broadcast_mac, NET_PORT_DHCP_SERVER, NET_PORT_DHCP_CLIENT,
                    message, length);
}

int net_dhcp(uint32_t timeout_ms)
{
    if (device == NULL)
        return -1;

    k_memset(&config, 0, sizeof(config)); /* source address 0.0.0.0 while asking */

    if (net_udp_listen(NET_PORT_DHCP_CLIENT, dhcp_handler, NULL) != 0)
        return -1;

    dhcp_xid = (uint32_t)hal_timer_ns() ^ 0x68480000u;
    dhcp_state = 0;
    k_memset(&dhcp_reply, 0, sizeof(dhcp_reply));

    uint64_t end = hal_timer_ns() + (uint64_t)timeout_ms * 1000000u;
    int result = -1;

    while (hal_timer_ns() < end && dhcp_state != 2)
    {
        /* Each try waits two seconds for the next answer, then asks again. */
        if (dhcp_state == 0)
            dhcp_send(NET_DHCP_DISCOVER);
        else
            dhcp_send(NET_DHCP_REQUEST);

        uint64_t try_end = hal_timer_ns() + 2000000000ull;
        int state_before = dhcp_state;

        while (hal_timer_ns() < try_end && hal_timer_ns() < end && dhcp_state == state_before)
        {
            net_poll();
            hal_cpu_idle();
        }
    }

    if (dhcp_state == 2)
    {
        net_configure(dhcp_reply.your_ip, dhcp_reply.mask, dhcp_reply.router, dhcp_reply.dns);
        config.lease_seconds = dhcp_reply.lease_seconds;
        result = 0;
    }

    net_udp_close(NET_PORT_DHCP_CLIENT);

    return result;
}

/* --- DNS --- */

static volatile int dns_done;
static int dns_result;
static uint16_t dns_id;
static uint8_t dns_address[4];

static void dns_handler(const uint8_t source_ip[4], uint16_t source_port,
                        const uint8_t *data, size_t length, void *context)
{
    uint8_t address[4];

    (void)source_ip;
    (void)source_port;
    (void)context;

    int status = net_dns_parse_response(data, length, dns_id, address);

    if (status < 0)
        return;

    dns_result = status;

    if (status == 0)
        k_memcpy(dns_address, address, 4);

    dns_done = 1;
}

int net_dns_resolve(const char *name, uint8_t ip[4], uint32_t timeout_ms)
{
    uint8_t query[300];
    static uint16_t next_port = 40000;

    if (device == NULL || !config.configured || is_zero_ip(config.dns))
        return -1;

    uint16_t port = next_port++;

    dns_id = (uint16_t)(hal_timer_ns() >> 10);
    size_t length = net_dns_build_query(query, sizeof(query), dns_id, name);

    if (length == 0 || net_udp_listen(port, dns_handler, NULL) != 0)
        return -1;

    dns_done = 0;
    dns_result = -1;

    int status = -1;

    if (net_udp_send(config.dns, NET_PORT_DNS, port, query, length) == 0 &&
        wait_for(&dns_done, timeout_ms) == 0)
    {
        status = dns_result;

        if (status == 0)
            k_memcpy(ip, dns_address, 4);
    }

    net_udp_close(port);

    return status;
}

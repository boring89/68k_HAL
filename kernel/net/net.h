/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_NET_NET_H
#define KERNEL_NET_NET_H

#include <stddef.h>
#include <stdint.h>

/*
 * A small network stack: Ethernet, ARP, IPv4, ICMP (ping), UDP, a DHCP
 * client and a DNS lookup. No TCP yet. One network card at a time.
 *
 * The stack does nothing on its own: net_poll() must be called often
 * (the main loop does, and every function below that waits). It then
 * takes the frames the card has received and answers or delivers them.
 * That keeps all protocol code out of interrupt handlers.
 */

#define NET_MTU 1500
#define NET_FRAME_MAX 1518 /* 14 header + 1500 payload + 4 checksum */

/* A network card, as the drivers present it. */
typedef struct net_device
{
    const char *name;  /* "e1000" */
    uint8_t mac[6];
    /* Send one Ethernet frame (header included, no checksum). Returns 0 or -1. */
    int (*send)(struct net_device *device, const void *frame, uint32_t length);
    /* Collect received frames and pass each to net_receive. */
    void (*poll)(struct net_device *device);
    void *priv;
} net_device_t;

/* Make device the card the stack uses. Returns 0, or -1 if there already is one. */
int net_register(net_device_t *device);

/* The registered card, or NULL. */
net_device_t *net_device_get(void);

/* For drivers: a frame arrived. The buffer is only valid during the call. */
void net_receive(net_device_t *device, const uint8_t *frame, uint32_t length);

/* Let the card hand over received frames. Cheap when there is no card. */
void net_poll(void);

/* The address configuration (all zero until DHCP or net_configure). */
typedef struct
{
    int configured;
    uint8_t ip[4];
    uint8_t mask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint32_t lease_seconds;
} net_config_t;

const net_config_t *net_config(void);

/* Set a fixed address (also used by tests). */
void net_configure(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gateway[4], const uint8_t dns[4]);

/*
 * Ask a DHCP server for an address and use what it says. Waits up to
 * timeout_ms in total (retrying the request). Returns 0 or -1.
 */
int net_dhcp(uint32_t timeout_ms);

/*
 * Send an ICMP echo request and wait for the reply. Returns 0 and the
 * round-trip time in microseconds, or -1 if no reply came within
 * timeout_ms (or the address cannot be reached).
 */
int net_ping(const uint8_t ip[4], uint32_t timeout_ms, uint32_t *round_trip_us);

/* Look up the IPv4 address of name with the configured DNS server. Returns 0, 1 if the name does not exist, or -1 on timeout/error. */
int net_dns_resolve(const char *name, uint8_t ip[4], uint32_t timeout_ms);

/* UDP: call handler for every datagram that arrives for port. */
typedef void (*net_udp_handler_t)(const uint8_t source_ip[4], uint16_t source_port,
                                  const uint8_t *data, size_t length, void *context);

int net_udp_listen(uint16_t port, net_udp_handler_t handler, void *context);
void net_udp_close(uint16_t port);

/* Send a UDP datagram (resolving the next hop with ARP). Returns 0 or -1. */
int net_udp_send(const uint8_t destination_ip[4], uint16_t destination_port,
                 uint16_t source_port, const void *data, size_t length);

/* Write "a.b.c.d" into text (at least 16 bytes). */
void net_format_ip(const uint8_t ip[4], char *text);

#endif

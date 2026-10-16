/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_NET_PACKET_H
#define KERNEL_NET_PACKET_H

#include <stddef.h>
#include <stdint.h>

/*
 * Building and reading the packets of the network protocols. Pure
 * functions on byte buffers (no hardware, no state), so the host tests
 * cover them. All multi-byte fields on the wire are big endian
 * ("network byte order"); addresses are plain byte arrays so that the
 * order is never in doubt.
 *
 * A packet is nested like envelopes:
 *
 *   Ethernet  [dst mac|src mac|type]
 *     ARP     "who has 10.0.2.2?"
 *     IPv4    [header|payload]
 *       ICMP  ping
 *       UDP   [ports|length|checksum|data]
 *         DHCP, DNS
 *
 * References: RFC 826 (ARP), RFC 791 (IPv4), RFC 792 (ICMP), RFC 768
 * (UDP), RFC 1071 (checksum), RFC 2131/2132 (DHCP), RFC 1035 (DNS).
 */

#define NET_ETH_HEADER 14
#define NET_ETHERTYPE_IPV4 0x0800
#define NET_ETHERTYPE_ARP 0x0806

#define NET_IP_HEADER 20
#define NET_IP_PROTO_ICMP 1
#define NET_IP_PROTO_UDP 17

#define NET_ICMP_ECHO_REPLY 0
#define NET_ICMP_ECHO_REQUEST 8

#define NET_UDP_HEADER 8
#define NET_ARP_PACKET 28
#define NET_ARP_REQUEST 1
#define NET_ARP_REPLY 2

#define NET_PORT_DHCP_SERVER 67
#define NET_PORT_DHCP_CLIENT 68
#define NET_PORT_DNS 53

#define NET_DHCP_DISCOVER 1
#define NET_DHCP_OFFER 2
#define NET_DHCP_REQUEST 3
#define NET_DHCP_ACK 5
#define NET_DHCP_NAK 6

/* Internet checksum (RFC 1071) of len bytes, continuing from sum (0 to start). Returns the folded, not yet inverted sum. */
uint32_t net_checksum_add(const void *data, size_t len, uint32_t sum);

/* Fold a running sum to 16 bits and invert it: the value to store in a checksum field. */
uint16_t net_checksum_finish(uint32_t sum);

/* --- Ethernet --- */

/* Write the 14-byte header; returns 14. */
size_t net_eth_build(uint8_t *out, const uint8_t dst[6], const uint8_t src[6], uint16_t type);

/* --- ARP --- */

typedef struct
{
    uint16_t operation; /* NET_ARP_REQUEST or NET_ARP_REPLY */
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
} net_arp_t;

size_t net_arp_build(uint8_t *out, const net_arp_t *arp);

/* Reads an Ethernet/IPv4 ARP packet. Returns 0 or -1 if it is something else or too short. */
int net_arp_parse(const uint8_t *data, size_t len, net_arp_t *arp);

/* --- IPv4 --- */

typedef struct
{
    uint8_t protocol;
    uint8_t source[4];
    uint8_t destination[4];
    uint8_t ttl;
    const uint8_t *payload; /* points into the packet that was parsed */
    size_t payload_length;
} net_ip_t;

/* Write a 20-byte header (no options, don't fragment, TTL 64) for payload_length bytes to follow; returns 20. */
size_t net_ip_build(uint8_t *out, uint8_t protocol, const uint8_t source[4],
                    const uint8_t destination[4], size_t payload_length, uint16_t id);

/*
 * Check and read an IPv4 packet: version 4, valid header length and
 * checksum, total length inside the buffer, not a fragment. Returns 0
 * or -1. Bytes after total length (Ethernet padding) are ignored.
 */
int net_ip_parse(const uint8_t *data, size_t len, net_ip_t *ip);

/* --- ICMP echo (ping) --- */

/* Write an echo request or reply with checksum; returns the length (8 + data_length). */
size_t net_icmp_echo_build(uint8_t *out, uint8_t type, uint16_t id, uint16_t sequence,
                           const uint8_t *data, size_t data_length);

typedef struct
{
    uint8_t type;
    uint16_t id;
    uint16_t sequence;
    const uint8_t *data;
    size_t data_length;
} net_icmp_echo_t;

/* Reads an ICMP echo request or reply and checks its checksum. Returns 0 or -1. */
int net_icmp_echo_parse(const uint8_t *data, size_t len, net_icmp_echo_t *echo);

/* --- UDP --- */

typedef struct
{
    uint16_t source_port;
    uint16_t destination_port;
    const uint8_t *data;
    size_t data_length;
} net_udp_t;

/* Write the header and copy the data (out must have room for 8 + data_length); checksum uses the IPv4 pseudo header. Returns the length. */
size_t net_udp_build(uint8_t *out, uint16_t source_port, uint16_t destination_port,
                     const uint8_t *data, size_t data_length,
                     const uint8_t source_ip[4], const uint8_t destination_ip[4]);

/* Read a UDP datagram; checks the length and, if the sender gave one, the checksum. Returns 0 or -1. */
int net_udp_parse(const uint8_t *data, size_t len, const uint8_t source_ip[4],
                  const uint8_t destination_ip[4], net_udp_t *udp);

/* --- DHCP --- */

typedef struct
{
    uint8_t type;       /* NET_DHCP_* */
    uint32_t xid;       /* transaction id */
    uint8_t your_ip[4]; /* the address offered or acknowledged */
    uint8_t mask[4];
    uint8_t router[4];
    uint8_t dns[4];
    uint8_t server[4];
    uint32_t lease_seconds;
    uint8_t client_mac[6];
} net_dhcp_t;

/*
 * Write a DHCP client message (BOOTP header plus options) of the given
 * type: DISCOVER, or REQUEST for requested_ip from server. Broadcast
 * flag set so that the reply reaches a client without an address yet.
 * Returns the length.
 */
size_t net_dhcp_build(uint8_t *out, uint8_t type, const uint8_t mac[6], uint32_t xid,
                      const uint8_t requested_ip[4], const uint8_t server[4]);

/* Read a DHCP reply (op 2, magic cookie, option 53 present). Returns 0 or -1. */
int net_dhcp_parse(const uint8_t *data, size_t len, net_dhcp_t *dhcp);

/* --- DNS --- */

/* Write a query for an A record of name ("example.com"). Returns the length, 0 if the name is invalid. */
size_t net_dns_build_query(uint8_t *out, size_t capacity, uint16_t id, const char *name);

/*
 * Read a response to the query with this id. Returns 0 and the first A
 * record in address, 1 if the server answered but there is no address
 * (name does not exist, empty answer), -1 if it is not a valid response
 * to the query.
 */
int net_dns_parse_response(const uint8_t *data, size_t len, uint16_t id, uint8_t address[4]);

#endif

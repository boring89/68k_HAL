/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "packet.h"

#include "kernel/lib/k_memory.h"

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)(v >> 16));
    put16(p + 2, (uint16_t)v);
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)get16(p) << 16) | get16(p + 2);
}

/* --- checksum --- */

uint32_t net_checksum_add(const void *data, size_t len, uint32_t sum)
{
    const uint8_t *p = data;

    /* Sum 16-bit big-endian words; an odd last byte is padded with zero. */
    for (; len >= 2; len -= 2, p += 2)
        sum += get16(p);

    if (len == 1)
        sum += (uint32_t)p[0] << 8;

    return sum;
}

uint16_t net_checksum_finish(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)~sum;
}

/* --- Ethernet --- */

size_t net_eth_build(uint8_t *out, const uint8_t dst[6], const uint8_t src[6], uint16_t type)
{
    k_memcpy(out, dst, 6);
    k_memcpy(out + 6, src, 6);
    put16(out + 12, type);

    return NET_ETH_HEADER;
}

/* --- ARP --- */

size_t net_arp_build(uint8_t *out, const net_arp_t *arp)
{
    put16(out, 1);      /* hardware type: Ethernet */
    put16(out + 2, NET_ETHERTYPE_IPV4);
    out[4] = 6;         /* hardware address length */
    out[5] = 4;         /* protocol address length */
    put16(out + 6, arp->operation);
    k_memcpy(out + 8, arp->sender_mac, 6);
    k_memcpy(out + 14, arp->sender_ip, 4);
    k_memcpy(out + 18, arp->target_mac, 6);
    k_memcpy(out + 24, arp->target_ip, 4);

    return NET_ARP_PACKET;
}

int net_arp_parse(const uint8_t *data, size_t len, net_arp_t *arp)
{
    if (len < NET_ARP_PACKET)
        return -1;

    if (get16(data) != 1 || get16(data + 2) != NET_ETHERTYPE_IPV4 || data[4] != 6 || data[5] != 4)
        return -1;

    arp->operation = get16(data + 6);
    k_memcpy(arp->sender_mac, data + 8, 6);
    k_memcpy(arp->sender_ip, data + 14, 4);
    k_memcpy(arp->target_mac, data + 18, 6);
    k_memcpy(arp->target_ip, data + 24, 4);

    return 0;
}

/* --- IPv4 --- */

size_t net_ip_build(uint8_t *out, uint8_t protocol, const uint8_t source[4],
                    const uint8_t destination[4], size_t payload_length, uint16_t id)
{
    out[0] = 0x45; /* version 4, header length 5 words */
    out[1] = 0;    /* type of service */
    put16(out + 2, (uint16_t)(NET_IP_HEADER + payload_length));
    put16(out + 4, id);
    put16(out + 6, 0x4000); /* don't fragment */
    out[8] = 64;            /* time to live */
    out[9] = protocol;
    put16(out + 10, 0);
    k_memcpy(out + 12, source, 4);
    k_memcpy(out + 16, destination, 4);
    put16(out + 10, net_checksum_finish(net_checksum_add(out, NET_IP_HEADER, 0)));

    return NET_IP_HEADER;
}

int net_ip_parse(const uint8_t *data, size_t len, net_ip_t *ip)
{
    if (len < NET_IP_HEADER || (data[0] >> 4) != 4)
        return -1;

    size_t header = (size_t)(data[0] & 0xF) * 4;
    size_t total = get16(data + 2);

    if (header < NET_IP_HEADER || header > len || total < header || total > len)
        return -1;

    /* A header with a correct checksum sums to 0xFFFF (inverted: 0). */
    if (net_checksum_finish(net_checksum_add(data, header, 0)) != 0)
        return -1;

    /* More-fragments flag or a fragment offset: reassembly is not supported. */
    if (get16(data + 6) & 0x3FFF)
        return -1;

    ip->protocol = data[9];
    ip->ttl = data[8];
    k_memcpy(ip->source, data + 12, 4);
    k_memcpy(ip->destination, data + 16, 4);
    ip->payload = data + header;
    ip->payload_length = total - header;

    return 0;
}

/* --- ICMP --- */

size_t net_icmp_echo_build(uint8_t *out, uint8_t type, uint16_t id, uint16_t sequence,
                           const uint8_t *data, size_t data_length)
{
    out[0] = type;
    out[1] = 0;
    put16(out + 2, 0);
    put16(out + 4, id);
    put16(out + 6, sequence);

    if (data_length > 0)
        k_memcpy(out + 8, data, data_length);

    put16(out + 2, net_checksum_finish(net_checksum_add(out, 8 + data_length, 0)));

    return 8 + data_length;
}

int net_icmp_echo_parse(const uint8_t *data, size_t len, net_icmp_echo_t *echo)
{
    if (len < 8 || (data[0] != NET_ICMP_ECHO_REQUEST && data[0] != NET_ICMP_ECHO_REPLY) || data[1] != 0)
        return -1;

    if (net_checksum_finish(net_checksum_add(data, len, 0)) != 0)
        return -1;

    echo->type = data[0];
    echo->id = get16(data + 4);
    echo->sequence = get16(data + 6);
    echo->data = data + 8;
    echo->data_length = len - 8;

    return 0;
}

/* --- UDP --- */

/* The pseudo header (RFC 768): addresses, protocol and UDP length join the checksum. */
static uint32_t udp_pseudo_sum(const uint8_t source_ip[4], const uint8_t destination_ip[4],
                               size_t udp_length)
{
    uint32_t sum = net_checksum_add(source_ip, 4, 0);

    sum = net_checksum_add(destination_ip, 4, sum);

    return sum + NET_IP_PROTO_UDP + (uint32_t)udp_length;
}

size_t net_udp_build(uint8_t *out, uint16_t source_port, uint16_t destination_port,
                     const uint8_t *data, size_t data_length,
                     const uint8_t source_ip[4], const uint8_t destination_ip[4])
{
    size_t length = NET_UDP_HEADER + data_length;

    put16(out, source_port);
    put16(out + 2, destination_port);
    put16(out + 4, (uint16_t)length);
    put16(out + 6, 0);

    if (data_length > 0)
        k_memcpy(out + NET_UDP_HEADER, data, data_length);

    uint16_t checksum = net_checksum_finish(
        net_checksum_add(out, length, udp_pseudo_sum(source_ip, destination_ip, length)));

    /* A computed checksum of zero is sent as all ones (zero means "no checksum"). */
    put16(out + 6, checksum == 0 ? 0xFFFF : checksum);

    return length;
}

int net_udp_parse(const uint8_t *data, size_t len, const uint8_t source_ip[4],
                  const uint8_t destination_ip[4], net_udp_t *udp)
{
    if (len < NET_UDP_HEADER)
        return -1;

    size_t length = get16(data + 4);

    if (length < NET_UDP_HEADER || length > len)
        return -1;

    if (get16(data + 6) != 0)
    {
        uint32_t sum = udp_pseudo_sum(source_ip, destination_ip, length);

        if (net_checksum_finish(net_checksum_add(data, length, sum)) != 0)
            return -1;
    }

    udp->source_port = get16(data);
    udp->destination_port = get16(data + 2);
    udp->data = data + NET_UDP_HEADER;
    udp->data_length = length - NET_UDP_HEADER;

    return 0;
}

/* --- DHCP --- */

#define DHCP_FIXED 236 /* BOOTP header up to and including "file" */
#define DHCP_COOKIE 0x63825363u

#define DHCP_OPT_MASK 1
#define DHCP_OPT_ROUTER 3
#define DHCP_OPT_DNS 6
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE 51
#define DHCP_OPT_TYPE 53
#define DHCP_OPT_SERVER 54
#define DHCP_OPT_PARAMETERS 55
#define DHCP_OPT_CLIENT_ID 61
#define DHCP_OPT_END 255

size_t net_dhcp_build(uint8_t *out, uint8_t type, const uint8_t mac[6], uint32_t xid,
                      const uint8_t requested_ip[4], const uint8_t server[4])
{
    k_memset(out, 0, DHCP_FIXED);
    out[0] = 1; /* op: BOOTREQUEST */
    out[1] = 1; /* hardware type: Ethernet */
    out[2] = 6;
    put32(out + 4, xid);
    put16(out + 10, 0x8000); /* flags: broadcast the reply */
    k_memcpy(out + 28, mac, 6);

    uint8_t *p = out + DHCP_FIXED;

    put32(p, DHCP_COOKIE);
    p += 4;

    *p++ = DHCP_OPT_TYPE;
    *p++ = 1;
    *p++ = type;

    *p++ = DHCP_OPT_CLIENT_ID;
    *p++ = 7;
    *p++ = 1; /* hardware type */
    k_memcpy(p, mac, 6);
    p += 6;

    if (type == NET_DHCP_REQUEST)
    {
        *p++ = DHCP_OPT_REQUESTED_IP;
        *p++ = 4;
        k_memcpy(p, requested_ip, 4);
        p += 4;

        *p++ = DHCP_OPT_SERVER;
        *p++ = 4;
        k_memcpy(p, server, 4);
        p += 4;
    }

    *p++ = DHCP_OPT_PARAMETERS;
    *p++ = 3;
    *p++ = DHCP_OPT_MASK;
    *p++ = DHCP_OPT_ROUTER;
    *p++ = DHCP_OPT_DNS;

    *p++ = DHCP_OPT_END;

    return (size_t)(p - out);
}

int net_dhcp_parse(const uint8_t *data, size_t len, net_dhcp_t *dhcp)
{
    if (len < DHCP_FIXED + 4 + 1 || data[0] != 2 || get32(data + DHCP_FIXED) != DHCP_COOKIE)
        return -1;

    k_memset(dhcp, 0, sizeof(*dhcp));
    dhcp->xid = get32(data + 4);
    k_memcpy(dhcp->your_ip, data + 16, 4);
    k_memcpy(dhcp->client_mac, data + 28, 6);

    int have_type = 0;
    size_t i = DHCP_FIXED + 4;

    while (i < len)
    {
        uint8_t code = data[i++];

        if (code == 0)
            continue; /* padding */

        if (code == DHCP_OPT_END)
            break;

        if (i >= len)
            return -1;

        size_t option_length = data[i++];

        if (option_length > len - i)
            return -1;

        const uint8_t *value = data + i;

        switch (code)
        {
        case DHCP_OPT_TYPE:
            if (option_length >= 1)
            {
                dhcp->type = value[0];
                have_type = 1;
            }
            break;

        case DHCP_OPT_MASK:
            if (option_length >= 4)
                k_memcpy(dhcp->mask, value, 4);
            break;

        case DHCP_OPT_ROUTER:
            if (option_length >= 4)
                k_memcpy(dhcp->router, value, 4); /* the first one */
            break;

        case DHCP_OPT_DNS:
            if (option_length >= 4)
                k_memcpy(dhcp->dns, value, 4);
            break;

        case DHCP_OPT_SERVER:
            if (option_length >= 4)
                k_memcpy(dhcp->server, value, 4);
            break;

        case DHCP_OPT_LEASE:
            if (option_length >= 4)
                dhcp->lease_seconds = get32(value);
            break;

        default:
            break;
        }

        i += option_length;
    }

    return have_type ? 0 : -1;
}

/* --- DNS --- */

#define DNS_HEADER 12
#define DNS_TYPE_A 1
#define DNS_CLASS_IN 1

size_t net_dns_build_query(uint8_t *out, size_t capacity, uint16_t id, const char *name)
{
    size_t i = DNS_HEADER;

    if (capacity < DNS_HEADER + 6)
        return 0;

    k_memset(out, 0, DNS_HEADER);
    put16(out, id);
    put16(out + 2, 0x0100); /* standard query, recursion desired */
    put16(out + 4, 1);      /* one question */

    /* The name as length-prefixed labels: "a.bc" -> 1 'a' 2 'b' 'c' 0. */
    while (*name != '\0')
    {
        size_t label = 0;

        while (name[label] != '\0' && name[label] != '.')
            label++;

        if (label == 0 || label > 63 || i + 1 + label + 5 > capacity)
            return 0;

        out[i++] = (uint8_t)label;

        for (size_t j = 0; j < label; j++)
            out[i++] = (uint8_t)name[j];

        name += label;

        if (*name == '.')
            name++;
    }

    if (i == DNS_HEADER || i + 5 > capacity)
        return 0;

    out[i++] = 0;
    put16(out + i, DNS_TYPE_A);
    put16(out + i + 2, DNS_CLASS_IN);

    return i + 4;
}

/* Skip a name (labels or a compression pointer). Returns the offset after it, or 0 if malformed. */
static size_t dns_skip_name(const uint8_t *data, size_t len, size_t i)
{
    while (i < len)
    {
        uint8_t n = data[i];

        if (n == 0)
            return i + 1;

        if ((n & 0xC0) == 0xC0)
            return i + 2 <= len ? i + 2 : 0; /* pointer: the name ends here */

        if (n & 0xC0)
            return 0;

        i += 1 + (size_t)n;
    }

    return 0;
}

int net_dns_parse_response(const uint8_t *data, size_t len, uint16_t id, uint8_t address[4])
{
    if (len < DNS_HEADER || get16(data) != id || !(data[2] & 0x80))
        return -1;

    unsigned int rcode = data[3] & 0xF;
    size_t questions = get16(data + 4);
    size_t answers = get16(data + 6);
    size_t i = DNS_HEADER;

    for (size_t q = 0; q < questions; q++)
    {
        i = dns_skip_name(data, len, i);

        if (i == 0 || i + 4 > len)
            return -1;

        i += 4;
    }

    if (rcode != 0)
        return 1;

    for (size_t a = 0; a < answers; a++)
    {
        i = dns_skip_name(data, len, i);

        if (i == 0 || i + 10 > len)
            return -1;

        uint16_t type = get16(data + i);
        uint16_t class = get16(data + i + 2);
        size_t rdlength = get16(data + i + 8);

        i += 10;

        if (rdlength > len - i)
            return -1;

        if (type == DNS_TYPE_A && class == DNS_CLASS_IN && rdlength == 4)
        {
            k_memcpy(address, data + i, 4);

            return 0;
        }

        i += rdlength;
    }

    return 1;
}

/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/net/packet.h"

#include <string.h>

static const uint8_t mac_a[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static const uint8_t mac_b[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
static const uint8_t ip_a[4] = {10, 0, 2, 15};
static const uint8_t ip_b[4] = {10, 0, 2, 2};

static void test_checksum(void)
{
    /* RFC 1071 section 3 example: 00 01 f2 03 f4 f5 f6 f7 -> sum ddf2, checksum 220d. */
    const uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};

    TEST_ASSERT_EQ_INT(0x220d, net_checksum_finish(net_checksum_add(data, sizeof(data), 0)));

    /* An odd length pads the last byte with zero. */
    const uint8_t odd[] = {0x12, 0x34, 0x56};

    TEST_ASSERT_EQ_INT(0x12 << 8 | 0x34, 0x1234);
    TEST_ASSERT_EQ_INT((uint16_t)~(0x1234 + 0x5600), net_checksum_finish(net_checksum_add(odd, 3, 0)));
}

static void test_arp(void)
{
    net_arp_t request = {NET_ARP_REQUEST, {0}, {0}, {0}, {0}};
    uint8_t packet[NET_ARP_PACKET];
    net_arp_t parsed;

    memcpy(request.sender_mac, mac_a, 6);
    memcpy(request.sender_ip, ip_a, 4);
    memcpy(request.target_ip, ip_b, 4);

    TEST_ASSERT_EQ_INT(NET_ARP_PACKET, net_arp_build(packet, &request));
    TEST_ASSERT_EQ_INT(0, net_arp_parse(packet, sizeof(packet), &parsed));
    TEST_ASSERT_EQ_INT(NET_ARP_REQUEST, parsed.operation);
    TEST_ASSERT(memcmp(parsed.sender_mac, mac_a, 6) == 0);
    TEST_ASSERT(memcmp(parsed.target_ip, ip_b, 4) == 0);

    TEST_ASSERT_EQ_INT(-1, net_arp_parse(packet, 27, &parsed));
    packet[4] = 8; /* wrong hardware address length */
    TEST_ASSERT_EQ_INT(-1, net_arp_parse(packet, sizeof(packet), &parsed));
}

static void test_ip(void)
{
    uint8_t packet[64];
    net_ip_t ip;

    memset(packet, 0, sizeof(packet));
    net_ip_build(packet, NET_IP_PROTO_UDP, ip_a, ip_b, 10, 7);

    TEST_ASSERT_EQ_INT(0, net_ip_parse(packet, 30, &ip));
    TEST_ASSERT_EQ_INT(NET_IP_PROTO_UDP, ip.protocol);
    TEST_ASSERT(memcmp(ip.source, ip_a, 4) == 0);
    TEST_ASSERT(memcmp(ip.destination, ip_b, 4) == 0);
    TEST_ASSERT_EQ_INT(10, ip.payload_length);
    TEST_ASSERT(ip.payload == packet + 20);

    /* Ethernet pads short frames: trailing bytes are ignored. */
    TEST_ASSERT_EQ_INT(0, net_ip_parse(packet, 60, &ip));
    TEST_ASSERT_EQ_INT(10, ip.payload_length);

    /* Too short, corrupted, a fragment, wrong version. */
    TEST_ASSERT_EQ_INT(-1, net_ip_parse(packet, 29, &ip));
    packet[8] ^= 1;
    TEST_ASSERT_EQ_INT(-1, net_ip_parse(packet, 30, &ip));
    packet[8] ^= 1;
    packet[0] = 0x65;
    TEST_ASSERT_EQ_INT(-1, net_ip_parse(packet, 30, &ip));
}

static void test_ip_fragment_rejected(void)
{
    uint8_t packet[40];
    net_ip_t ip;

    memset(packet, 0, sizeof(packet));
    net_ip_build(packet, NET_IP_PROTO_ICMP, ip_a, ip_b, 8, 1);
    packet[6] = 0x20; /* more fragments */
    packet[10] = packet[11] = 0;

    uint16_t sum = net_checksum_finish(net_checksum_add(packet, 20, 0));

    packet[10] = (uint8_t)(sum >> 8);
    packet[11] = (uint8_t)sum;
    TEST_ASSERT_EQ_INT(-1, net_ip_parse(packet, 28, &ip));
}

static void test_icmp(void)
{
    uint8_t packet[64];
    const uint8_t payload[] = "abcdefgh";
    net_icmp_echo_t echo;

    size_t length = net_icmp_echo_build(packet, NET_ICMP_ECHO_REQUEST, 0x1234, 5, payload, 8);

    TEST_ASSERT_EQ_INT(16, length);
    TEST_ASSERT_EQ_INT(0, net_icmp_echo_parse(packet, length, &echo));
    TEST_ASSERT_EQ_INT(NET_ICMP_ECHO_REQUEST, echo.type);
    TEST_ASSERT_EQ_INT(0x1234, echo.id);
    TEST_ASSERT_EQ_INT(5, echo.sequence);
    TEST_ASSERT_EQ_INT(8, echo.data_length);
    TEST_ASSERT(memcmp(echo.data, payload, 8) == 0);

    packet[9] ^= 0xFF; /* corrupt the data */
    TEST_ASSERT_EQ_INT(-1, net_icmp_echo_parse(packet, length, &echo));
}

static void test_udp(void)
{
    uint8_t packet[64];
    const uint8_t data[] = {1, 2, 3, 4, 5};
    net_udp_t udp;

    size_t length = net_udp_build(packet, 1234, 53, data, 5, ip_a, ip_b);

    TEST_ASSERT_EQ_INT(13, length);
    TEST_ASSERT_EQ_INT(0, net_udp_parse(packet, length, ip_a, ip_b, &udp));
    TEST_ASSERT_EQ_INT(1234, udp.source_port);
    TEST_ASSERT_EQ_INT(53, udp.destination_port);
    TEST_ASSERT_EQ_INT(5, udp.data_length);
    TEST_ASSERT(memcmp(udp.data, data, 5) == 0);

    /* The pseudo header covers the addresses. */
    TEST_ASSERT_EQ_INT(-1, net_udp_parse(packet, length, ip_b, ip_b, &udp));

    /* Checksum 0 = none given. */
    packet[6] = packet[7] = 0;
    TEST_ASSERT_EQ_INT(0, net_udp_parse(packet, length, ip_b, ip_b, &udp));

    TEST_ASSERT_EQ_INT(-1, net_udp_parse(packet, 7, ip_a, ip_b, &udp));
    TEST_ASSERT_EQ_INT(-1, net_udp_parse(packet, length - 1, ip_a, ip_b, &udp)); /* length field too big */
}

static void test_dhcp_build(void)
{
    uint8_t packet[400];
    size_t length = net_dhcp_build(packet, NET_DHCP_DISCOVER, mac_a, 0xDEADBEEF, NULL, NULL);

    TEST_ASSERT(length > 240 && length < 300);
    TEST_ASSERT_EQ_INT(1, packet[0]);
    TEST_ASSERT_EQ_INT(0xDE, packet[4]);
    TEST_ASSERT_EQ_INT(0x80, packet[10]);
    TEST_ASSERT(memcmp(packet + 28, mac_a, 6) == 0);
    TEST_ASSERT_EQ_INT(0x63, packet[236]);
    TEST_ASSERT_EQ_INT(53, packet[240]);
    TEST_ASSERT_EQ_INT(NET_DHCP_DISCOVER, packet[242]);
    TEST_ASSERT_EQ_INT(255, packet[length - 1]);

    length = net_dhcp_build(packet, NET_DHCP_REQUEST, mac_a, 1, ip_a, ip_b);

    /* The request also carries the requested address (50) and server (54). */
    int saw_requested = 0, saw_server = 0;

    for (size_t i = 240; i < length;)
    {
        if (packet[i] == 255)
            break;

        if (packet[i] == 50 && memcmp(packet + i + 2, ip_a, 4) == 0)
            saw_requested = 1;

        if (packet[i] == 54 && memcmp(packet + i + 2, ip_b, 4) == 0)
            saw_server = 1;

        i += 2 + packet[i + 1];
    }

    TEST_ASSERT(saw_requested && saw_server);
}

/* A reply as QEMU's slirp sends it: offer of 10.0.2.15, router/dns/mask/lease/server options. */
static size_t make_dhcp_reply(uint8_t *p, uint8_t type)
{
    memset(p, 0, 400);
    p[0] = 2;
    p[1] = 1;
    p[2] = 6;
    p[4] = 0xDE; p[5] = 0xAD; p[6] = 0xBE; p[7] = 0xEF;
    p[16] = 10; p[17] = 0; p[18] = 2; p[19] = 15;
    memcpy(p + 28, mac_a, 6);
    p[236] = 0x63; p[237] = 0x82; p[238] = 0x53; p[239] = 0x63;

    uint8_t *o = p + 240;
    const uint8_t options[] = {
        53, 1, type,
        1, 4, 255, 255, 255, 0,
        3, 4, 10, 0, 2, 2,
        6, 4, 10, 0, 2, 3,
        51, 4, 0, 0, 0x0E, 0x10, /* 3600 s */
        54, 4, 10, 0, 2, 2,
        0, 0, /* padding */
        255,
    };

    memcpy(o, options, sizeof(options));

    return 240 + sizeof(options);
}

static void test_dhcp_parse(void)
{
    uint8_t packet[400];
    net_dhcp_t dhcp;
    size_t length = make_dhcp_reply(packet, NET_DHCP_OFFER);

    TEST_ASSERT_EQ_INT(0, net_dhcp_parse(packet, length, &dhcp));
    TEST_ASSERT_EQ_INT(NET_DHCP_OFFER, dhcp.type);
    TEST_ASSERT_EQ_INT(0xDEADBEEF, dhcp.xid);
    TEST_ASSERT(memcmp(dhcp.your_ip, ip_a, 4) == 0);
    TEST_ASSERT_EQ_INT(255, dhcp.mask[0]);
    TEST_ASSERT_EQ_INT(0, dhcp.mask[3]);
    TEST_ASSERT(memcmp(dhcp.router, ip_b, 4) == 0);
    TEST_ASSERT_EQ_INT(3, dhcp.dns[3]);
    TEST_ASSERT(memcmp(dhcp.server, ip_b, 4) == 0);
    TEST_ASSERT_EQ_INT(3600, dhcp.lease_seconds);

    /* Not a reply, no cookie, option running past the end, no message type. */
    packet[0] = 1;
    TEST_ASSERT_EQ_INT(-1, net_dhcp_parse(packet, length, &dhcp));
    length = make_dhcp_reply(packet, NET_DHCP_ACK);
    packet[236] = 0;
    TEST_ASSERT_EQ_INT(-1, net_dhcp_parse(packet, length, &dhcp));
    length = make_dhcp_reply(packet, NET_DHCP_ACK);
    TEST_ASSERT_EQ_INT(-1, net_dhcp_parse(packet, 244, &dhcp));
    packet[240] = 99; /* unknown option instead of the type */
    TEST_ASSERT_EQ_INT(-1, net_dhcp_parse(packet, length, &dhcp));
}

static void test_dns_query(void)
{
    uint8_t packet[64];
    size_t length = net_dns_build_query(packet, sizeof(packet), 0xBEEF, "example.com");

    /* header 12 + 7 'example' + 3 'com' + root + type + class */
    TEST_ASSERT_EQ_INT(12 + 1 + 7 + 1 + 3 + 1 + 4, length);
    TEST_ASSERT_EQ_INT(0xBE, packet[0]);
    TEST_ASSERT_EQ_INT(1, packet[5]);
    TEST_ASSERT_EQ_INT(7, packet[12]);
    TEST_ASSERT(memcmp(packet + 13, "example", 7) == 0);
    TEST_ASSERT_EQ_INT(3, packet[20]);
    TEST_ASSERT_EQ_INT(0, packet[24]);
    TEST_ASSERT_EQ_INT(1, packet[26]); /* type A */

    TEST_ASSERT_EQ_INT(0, net_dns_build_query(packet, sizeof(packet), 1, ""));
    TEST_ASSERT_EQ_INT(0, net_dns_build_query(packet, sizeof(packet), 1, "a..b"));
    TEST_ASSERT_EQ_INT(0, net_dns_build_query(packet, 20, 1, "example.com"));
}

static void test_dns_response(void)
{
    uint8_t query[64];
    uint8_t response[128];
    uint8_t address[4];
    size_t qlen = net_dns_build_query(query, sizeof(query), 0x1234, "example.com");

    /* Echo the question, then one CNAME answer and one A answer (name compressed with a pointer). */
    memcpy(response, query, qlen);
    response[2] = 0x81; response[3] = 0x80;
    response[6] = 0; response[7] = 2;
    size_t i = qlen;
    const uint8_t cname[] = {0xC0, 12, 0, 5, 0, 1, 0, 0, 0, 60, 0, 2, 0xC0, 12};
    const uint8_t a[] = {0xC0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 93, 184, 216, 34};

    memcpy(response + i, cname, sizeof(cname));
    i += sizeof(cname);
    memcpy(response + i, a, sizeof(a));
    i += sizeof(a);

    TEST_ASSERT_EQ_INT(0, net_dns_parse_response(response, i, 0x1234, address));
    TEST_ASSERT_EQ_INT(93, address[0]);
    TEST_ASSERT_EQ_INT(34, address[3]);

    /* Wrong id, not a response, truncated. */
    TEST_ASSERT_EQ_INT(-1, net_dns_parse_response(response, i, 0x1235, address));
    response[2] = 0x01;
    TEST_ASSERT_EQ_INT(-1, net_dns_parse_response(response, i, 0x1234, address));
    response[2] = 0x81;
    TEST_ASSERT_EQ_INT(-1, net_dns_parse_response(response, i - 3, 0x1234, address));

    /* The server says no such name (NXDOMAIN), or the answer has no A record. */
    response[3] = 0x83;
    TEST_ASSERT_EQ_INT(1, net_dns_parse_response(response, i, 0x1234, address));
    response[3] = 0x80;
    response[7] = 0;
    TEST_ASSERT_EQ_INT(1, net_dns_parse_response(response, i, 0x1234, address));
}

static void test_eth(void)
{
    uint8_t frame[14];

    TEST_ASSERT_EQ_INT(14, net_eth_build(frame, mac_b, mac_a, NET_ETHERTYPE_ARP));
    TEST_ASSERT(memcmp(frame, mac_b, 6) == 0);
    TEST_ASSERT(memcmp(frame + 6, mac_a, 6) == 0);
    TEST_ASSERT_EQ_INT(0x08, frame[12]);
    TEST_ASSERT_EQ_INT(0x06, frame[13]);
}

int main(void)
{
    TEST_RUN(test_checksum);
    TEST_RUN(test_eth);
    TEST_RUN(test_arp);
    TEST_RUN(test_ip);
    TEST_RUN(test_ip_fragment_rejected);
    TEST_RUN(test_icmp);
    TEST_RUN(test_udp);
    TEST_RUN(test_dhcp_build);
    TEST_RUN(test_dhcp_parse);
    TEST_RUN(test_dns_query);
    TEST_RUN(test_dns_response);

    return TEST_REPORT("net_packet");
}

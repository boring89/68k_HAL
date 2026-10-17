/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/usb/descriptor.h"

#include <string.h>

/* QEMU's usb-kbd device descriptor (USB 2.0, 64-byte control endpoint). */
static void test_device(void)
{
    static const uint8_t data[18] = {18, 1, 0x00, 0x02, 0, 0, 0, 64, 0x27, 0x06, 0x01, 0x00, 0x00, 0x00, 1, 2, 3, 1};
    usb_device_descriptor_t d;

    TEST_ASSERT_EQ_INT(0, usb_parse_device(data, 18, &d));
    TEST_ASSERT_EQ_INT(0x0200, d.usb_version);
    TEST_ASSERT_EQ_INT(64, d.max_packet_size0);
    TEST_ASSERT_EQ_INT(0x0627, d.vendor_id);
    TEST_ASSERT_EQ_INT(0x0001, d.product_id);
    TEST_ASSERT_EQ_INT(1, d.configuration_count);

    TEST_ASSERT_EQ_INT(-1, usb_parse_device(data, 17, &d));

    uint8_t bad[18];

    memcpy(bad, data, 18);
    bad[1] = 2;
    TEST_ASSERT_EQ_INT(-1, usb_parse_device(bad, 18, &d));

    /* SuperSpeed: bMaxPacketSize0 = 9 means 512. */
    memcpy(bad, data, 18);
    bad[3] = 0x03;
    bad[7] = 9;
    TEST_ASSERT_EQ_INT(0, usb_parse_device(bad, 18, &d));
    TEST_ASSERT_EQ_INT(0xFF, d.max_packet_size0); /* does not fit a byte: the caller knows SuperSpeed means 512 */
    bad[7] = 6;
    usb_parse_device(bad, 18, &d);
    TEST_ASSERT_EQ_INT(64, d.max_packet_size0);
}

/* A keyboard configuration: configuration, interface (HID boot keyboard), HID descriptor, endpoint. */
static const uint8_t keyboard_config[] = {
    9, 2, 34, 0, 1, 1, 0, 0xA0, 50,
    9, 4, 0, 0, 1, 3, 1, 1, 0,
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,
    7, 5, 0x81, 3, 8, 0, 10,
};

static void test_keyboard(void)
{
    usb_hid_interface_t found[USB_MAX_HID_INTERFACES];
    int hub = 7;

    TEST_ASSERT_EQ_INT(34, usb_configuration_total_length(keyboard_config, 9));
    TEST_ASSERT_EQ_INT(1, usb_parse_configuration(keyboard_config, sizeof(keyboard_config), found, 4, &hub));
    TEST_ASSERT_EQ_INT(0, hub);
    TEST_ASSERT_EQ_INT(1, found[0].configuration_value);
    TEST_ASSERT_EQ_INT(0, found[0].interface_number);
    TEST_ASSERT_EQ_INT(USB_HID_PROTOCOL_KEYBOARD, found[0].protocol);
    TEST_ASSERT_EQ_INT(0x81, found[0].endpoint_address);
    TEST_ASSERT_EQ_INT(8, found[0].max_packet_size);
    TEST_ASSERT_EQ_INT(10, found[0].interval);

    /* Truncated: the declared length is longer than what we have. */
    TEST_ASSERT_EQ_INT(-1, usb_parse_configuration(keyboard_config, sizeof(keyboard_config) - 1, found, 4, &hub));
}

/* A composite device: a keyboard and a mouse interface, and a vendor-specific one that is skipped. */
static void test_composite(void)
{
    static const uint8_t config[] = {
        9, 2, 57, 0, 3, 1, 0, 0xA0, 50,
        9, 4, 0, 0, 1, 3, 1, 1, 0,
        7, 5, 0x81, 3, 8, 0, 10,
        9, 4, 1, 0, 1, 3, 1, 2, 0,
        7, 5, 0x82, 3, 4, 0, 4,
        9, 4, 2, 0, 1, 0xFF, 0, 0, 0,
        7, 5, 0x83, 3, 8, 0, 10,
    };
    usb_hid_interface_t found[USB_MAX_HID_INTERFACES];
    int hub = 0;

    TEST_ASSERT_EQ_INT(2, usb_parse_configuration(config, sizeof(config), found, 4, &hub));
    TEST_ASSERT_EQ_INT(USB_HID_PROTOCOL_KEYBOARD, found[0].protocol);
    TEST_ASSERT_EQ_INT(1, found[1].interface_number);
    TEST_ASSERT_EQ_INT(USB_HID_PROTOCOL_MOUSE, found[1].protocol);
    TEST_ASSERT_EQ_INT(0x82, found[1].endpoint_address);
    TEST_ASSERT_EQ_INT(4, found[1].interval);

    /* Room for one: only the first is reported. */
    TEST_ASSERT_EQ_INT(1, usb_parse_configuration(config, sizeof(config), found, 1, &hub));
}

static void test_hub_and_other(void)
{
    static const uint8_t hub_config[] = {
        9, 2, 25, 0, 1, 1, 0, 0xE0, 0,
        9, 4, 0, 0, 1, 9, 0, 0, 0,
        7, 5, 0x81, 3, 1, 0, 12,
    };
    usb_hid_interface_t found[USB_MAX_HID_INTERFACES];
    int hub = 0;

    TEST_ASSERT_EQ_INT(0, usb_parse_configuration(hub_config, sizeof(hub_config), found, 4, &hub));
    TEST_ASSERT_EQ_INT(1, hub);

    /* A HID interface that is not boot protocol (subclass 0) is not used. */
    uint8_t other[sizeof(keyboard_config)];

    memcpy(other, keyboard_config, sizeof(other));
    other[9 + 6] = 0;
    TEST_ASSERT_EQ_INT(0, usb_parse_configuration(other, sizeof(other), found, 4, &hub));

    /* An OUT endpoint or a bulk one is not used. */
    memcpy(other, keyboard_config, sizeof(other));
    other[27 + 2] = 0x01;
    TEST_ASSERT_EQ_INT(0, usb_parse_configuration(other, sizeof(other), found, 4, &hub));

    TEST_ASSERT_EQ_INT(0, usb_configuration_total_length(keyboard_config, 5));
}

/* QEMU's usb-storage: one Bulk-Only SCSI interface with a bulk IN (0x81) and a bulk OUT (0x02) endpoint. */
static void test_storage(void)
{
    static const uint8_t config[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 8, 6, 0x50, 0,
        7, 5, 0x81, 2, 0, 2, 0,
        7, 5, 0x02, 2, 0, 2, 0,
    };
    usb_storage_interface_t found;

    TEST_ASSERT_EQ_INT(0, usb_find_storage(config, sizeof(config), &found));
    TEST_ASSERT_EQ_INT(1, found.configuration_value);
    TEST_ASSERT_EQ_INT(0, found.interface_number);
    TEST_ASSERT_EQ_INT(0x81, found.bulk_in);
    TEST_ASSERT_EQ_INT(0x02, found.bulk_out);
    TEST_ASSERT_EQ_INT(512, found.in_packet_size);

    /* The endpoints may come in the other order; a keyboard is not storage; a missing OUT endpoint is not usable. */
    uint8_t swapped[sizeof(config)];

    memcpy(swapped, config, sizeof(config));
    memcpy(swapped + 18, config + 25, 7);
    memcpy(swapped + 25, config + 18, 7);
    TEST_ASSERT_EQ_INT(0, usb_find_storage(swapped, sizeof(swapped), &found));
    TEST_ASSERT_EQ_INT(0x81, found.bulk_in);

    TEST_ASSERT_EQ_INT(-1, usb_find_storage(keyboard_config, sizeof(keyboard_config), &found));

    uint8_t one_way[sizeof(config)];

    memcpy(one_way, config, sizeof(config));
    one_way[25 + 2] = 0x81; /* both IN */
    memcpy(one_way + 18, one_way + 25, 7);
    TEST_ASSERT_EQ_INT(-1, usb_find_storage(one_way, sizeof(one_way), &found));
}

int main(void)
{
    TEST_RUN(test_device);
    TEST_RUN(test_keyboard);
    TEST_RUN(test_composite);
    TEST_RUN(test_hub_and_other);
    TEST_RUN(test_storage);

    return TEST_REPORT("usb_descriptor");
}

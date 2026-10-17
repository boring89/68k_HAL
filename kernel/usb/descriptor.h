/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USB_DESCRIPTOR_H
#define KERNEL_USB_DESCRIPTOR_H

#include <stdint.h>

/*
 * USB descriptors (Universal Serial Bus Specification 2.0, chapter 9.6; Device Class Definition
 * for HID 1.11): the little tables a device sends when asked, saying what it is. Parsed from bytes
 * (no structs cast over the buffer, so alignment and byte order never matter), so this runs
 * in the host tests too.
 */

#define USB_DT_DEVICE 1
#define USB_DT_CONFIGURATION 2
#define USB_DT_INTERFACE 4
#define USB_DT_ENDPOINT 5
#define USB_DT_HUB 0x29

#define USB_CLASS_HID 3
#define USB_CLASS_HUB 9
#define USB_HID_SUBCLASS_BOOT 1
#define USB_HID_PROTOCOL_KEYBOARD 1
#define USB_HID_PROTOCOL_MOUSE 2

typedef struct
{
    uint16_t usb_version; /* BCD, 0x0200 = USB 2.0 */
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t max_packet_size0; /* bMaxPacketSize0 (for SuperSpeed it is 2^n: decoded here) */
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t configuration_count;
} usb_device_descriptor_t;

/* Parse the 18-byte device descriptor (at least 8 bytes are needed for max_packet_size0 alone: see below). Returns 0, or -1. */
int usb_parse_device(const uint8_t *data, unsigned length, usb_device_descriptor_t *out);

/* An interface of a configuration worth using: a HID boot keyboard or mouse. */
typedef struct
{
    uint8_t configuration_value; /* for SET_CONFIGURATION */
    uint8_t interface_number;
    uint8_t protocol;            /* USB_HID_PROTOCOL_KEYBOARD or _MOUSE */
    uint8_t endpoint_address;    /* the interrupt IN endpoint, with bit 7 set */
    uint16_t max_packet_size;
    uint8_t interval;            /* bInterval */
} usb_hid_interface_t;

#define USB_MAX_HID_INTERFACES 4

/*
 * Walk a configuration descriptor (the whole thing, with its interfaces and endpoints) and
 * list the HID boot-protocol keyboards and mice that have an interrupt IN endpoint. Returns
 * how many (at most max), or -1 if the descriptor is malformed. *is_hub is set to 1 if the
 * configuration has a hub interface (class 9).
 */
int usb_parse_configuration(const uint8_t *data, unsigned length, usb_hid_interface_t *out,
                            unsigned max, int *is_hub);

/* wTotalLength of a configuration descriptor from its first 9 bytes, or 0 if they are not one. */
unsigned usb_configuration_total_length(const uint8_t *data, unsigned length);

#endif

/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USB_USB_H
#define KERNEL_USB_USB_H

#include <stdint.h>

/*
 * USB host controllers: the chips that drive the USB ports. Only the first
 * step of USB support exists, finding them: a driver registers each
 * controller it recognises here and `usb` lists them. Talking to devices
 * (reset a port, give the device an address, read its descriptors, run
 * transfers) is the next step; see docs/usb.md for the plan.
 */

typedef enum
{
    USB_HOST_UHCI, /* USB 1.x, Intel/VIA, I/O registers */
    USB_HOST_OHCI, /* USB 1.x, Compaq/Apple/..., memory registers */
    USB_HOST_EHCI, /* USB 2.0 high speed */
    USB_HOST_XHCI  /* USB 3.x (and everything older through its ports) */
} usb_host_type_t;

#define USB_MAX_HOSTS 8

typedef struct
{
    usb_host_type_t type;
    uint8_t bus;      /* where it is on the PCI bus */
    uint8_t device;
    uint8_t function;
    uint16_t version; /* the controller's interface version, BCD (0x0100 = 1.0) */
    uint8_t ports;    /* root hub ports; 0 if not read */
    uint8_t slots;    /* xHCI: device slots (how many devices it can run); else 0 */
} usb_host_info_t;

/* Add a controller to the list. Returns 0, or -1 if the list is full. */
int usb_host_register(const usb_host_info_t *host);

unsigned int usb_host_count(void);

/* Controller index. Returns 0, or -1 if out of range. */
int usb_host_get(unsigned int index, usb_host_info_t *host);

/* "UHCI", "OHCI", "EHCI" or "xHCI". */
const char *usb_host_type_name(usb_host_type_t type);

/* The USB version a controller type implements: "1.1", "2.0" or "3.x". */
const char *usb_host_speed_name(usb_host_type_t type);

/* Forget every controller (for tests). */
void usb_reset(void);

#endif

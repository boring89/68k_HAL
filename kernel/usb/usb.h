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

/* A device found on a port of a host controller (what `usb` lists below the controllers). */
#define USB_MAX_DEVICES 16

typedef struct
{
    uint8_t port;         /* root hub port, 1-based */
    uint8_t speed;        /* 1 full, 2 low, 3 high, 4 super, 5 super+ */
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t device_class;
    char use[16];         /* "keyboard", "mouse", "hub", "not used"... */
} usb_device_info_t;

/* Add a device to the list. Returns its id (>= 0), or -1 if the list is full. */
int usb_device_add(const usb_device_info_t *device);

/* Take it off the list again (unplugged). */
void usb_device_remove(int id);

unsigned int usb_device_count(void);

/* Device index of those on the list. Returns 0, or -1 if out of range. */
int usb_device_get(unsigned int index, usb_device_info_t *device);

/*
 * Drivers that need to look at their hardware now and then (hot plug, a lost interrupt) register a
 * poller. usb_poll runs them all; the kernel's main loop and the scheduler's idle loop call it.
 * Never from an interrupt handler. At most USB_MAX_POLLERS.
 */
#define USB_MAX_POLLERS 4

int usb_poller_register(void (*poller)(void));
void usb_poll(void);

/* Forget every controller (for tests). */
void usb_reset(void);

#endif

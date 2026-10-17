/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "usb.h"

static usb_host_info_t hosts[USB_MAX_HOSTS];
static unsigned int host_total;

static usb_device_info_t devices[USB_MAX_DEVICES];
static uint8_t device_used[USB_MAX_DEVICES];

int usb_device_add(const usb_device_info_t *device)
{
    for (int i = 0; i < USB_MAX_DEVICES; i++)
    {
        if (!device_used[i])
        {
            devices[i] = *device;
            device_used[i] = 1;

            return i;
        }
    }

    return -1;
}

void usb_device_remove(int id)
{
    if (id >= 0 && id < USB_MAX_DEVICES)
        device_used[id] = 0;
}

unsigned int usb_device_count(void)
{
    unsigned int count = 0;

    for (int i = 0; i < USB_MAX_DEVICES; i++)
        count += device_used[i];

    return count;
}

int usb_device_get(unsigned int index, usb_device_info_t *device)
{
    for (int i = 0; i < USB_MAX_DEVICES; i++)
    {
        if (!device_used[i])
            continue;

        if (index-- == 0)
        {
            *device = devices[i];

            return 0;
        }
    }

    return -1;
}

static void (*pollers[USB_MAX_POLLERS])(void);
static unsigned int poller_total;

int usb_poller_register(void (*poller)(void))
{
    if (poller == 0 || poller_total == USB_MAX_POLLERS)
        return -1;

    pollers[poller_total++] = poller;

    return 0;
}

void usb_poll(void)
{
    for (unsigned int i = 0; i < poller_total; i++)
        pollers[i]();
}

int usb_host_register(const usb_host_info_t *host)
{
    if (host == 0 || host_total == USB_MAX_HOSTS)
        return -1;

    hosts[host_total++] = *host;

    return 0;
}

unsigned int usb_host_count(void)
{
    return host_total;
}

int usb_host_get(unsigned int index, usb_host_info_t *host)
{
    if (index >= host_total || host == 0)
        return -1;

    *host = hosts[index];

    return 0;
}

const char *usb_host_type_name(usb_host_type_t type)
{
    switch (type)
    {
    case USB_HOST_UHCI:
        return "UHCI";

    case USB_HOST_OHCI:
        return "OHCI";

    case USB_HOST_EHCI:
        return "EHCI";

    default:
        return "xHCI";
    }
}

const char *usb_host_speed_name(usb_host_type_t type)
{
    switch (type)
    {
    case USB_HOST_UHCI:
    case USB_HOST_OHCI:
        return "1.1";

    case USB_HOST_EHCI:
        return "2.0";

    default:
        return "3.x";
    }
}

void usb_reset(void)
{
    host_total = 0;

    for (int i = 0; i < USB_MAX_DEVICES; i++)
        device_used[i] = 0;
}

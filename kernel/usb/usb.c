/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "usb.h"

static usb_host_info_t hosts[USB_MAX_HOSTS];
static unsigned int host_total;

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
}

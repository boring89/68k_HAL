/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "descriptor.h"

#include <stddef.h>

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

int usb_parse_device(const uint8_t *data, unsigned length, usb_device_descriptor_t *out)
{
    if (length < 18 || data[0] < 18 || data[1] != USB_DT_DEVICE)
        return -1;

    out->usb_version = le16(data + 2);
    out->device_class = data[4];
    out->device_subclass = data[5];
    out->device_protocol = data[6];
    /* USB 3 devices give the exponent (9 = 512 bytes); USB 2 devices the size itself. */
    out->max_packet_size0 = out->usb_version >= 0x0300 ? (uint8_t)(data[7] >= 8 ? 0xFF : 1u << data[7]) : data[7];
    out->vendor_id = le16(data + 8);
    out->product_id = le16(data + 10);
    out->configuration_count = data[17];

    return 0;
}

unsigned usb_configuration_total_length(const uint8_t *data, unsigned length)
{
    if (length < 9 || data[0] < 9 || data[1] != USB_DT_CONFIGURATION)
        return 0;

    return le16(data + 2);
}

int usb_parse_configuration(const uint8_t *data, unsigned length, usb_hid_interface_t *out,
                            unsigned max, int *is_hub)
{
    unsigned total = usb_configuration_total_length(data, length);

    if (total == 0 || total > length)
        return -1;

    unsigned count = 0;
    unsigned offset = 0;
    uint8_t configuration_value = data[5];

    /* The interface being read: the next endpoint descriptors belong to it. */
    int in_boot = 0;
    uint8_t interface_number = 0;
    uint8_t protocol = 0;

    *is_hub = 0;

    while (offset + 2 <= total)
    {
        uint8_t size = data[offset];
        uint8_t type = data[offset + 1];

        if (size < 2 || offset + size > total)
            return -1;

        if (type == USB_DT_INTERFACE && size >= 9)
        {
            uint8_t interface_class = data[offset + 5];
            uint8_t subclass = data[offset + 6];

            in_boot = interface_class == USB_CLASS_HID && subclass == USB_HID_SUBCLASS_BOOT &&
                      (data[offset + 7] == USB_HID_PROTOCOL_KEYBOARD || data[offset + 7] == USB_HID_PROTOCOL_MOUSE);
            interface_number = data[offset + 2];
            protocol = data[offset + 7];

            if (interface_class == USB_CLASS_HUB)
                *is_hub = 1;
        }
        else if (type == USB_DT_ENDPOINT && size >= 7 && in_boot)
        {
            uint8_t address = data[offset + 2];
            uint8_t attributes = data[offset + 3];

            /* An interrupt (transfer type 3) IN (bit 7) endpoint. */
            if ((address & 0x80) && (attributes & 3) == 3 && count < max)
            {
                out[count].configuration_value = configuration_value;
                out[count].interface_number = interface_number;
                out[count].protocol = protocol;
                out[count].endpoint_address = address;
                out[count].max_packet_size = le16(data + offset + 4);
                out[count].interval = data[offset + 6];
                count++;
                in_boot = 0; /* one endpoint per interface is enough */
            }
        }

        offset += size;
    }

    return (int)count;
}

int usb_find_storage(const uint8_t *data, unsigned length, usb_storage_interface_t *out)
{
    unsigned total = usb_configuration_total_length(data, length);

    if (total == 0 || total > length)
        return -1;

    int in_storage = 0;
    uint8_t interface_number = 0;
    usb_storage_interface_t found = {0};

    found.configuration_value = data[5];

    for (unsigned offset = 0; offset + 2 <= total;)
    {
        uint8_t size = data[offset];
        uint8_t type = data[offset + 1];

        if (size < 2 || offset + size > total)
            return -1;

        if (type == USB_DT_INTERFACE && size >= 9)
        {
            in_storage = data[offset + 5] == USB_CLASS_MASS_STORAGE && data[offset + 6] == USB_SUBCLASS_SCSI &&
                         data[offset + 7] == USB_PROTOCOL_BULK_ONLY;
            interface_number = data[offset + 2];
            found.interface_number = interface_number;
            found.bulk_in = 0;
            found.bulk_out = 0;
        }
        else if (type == USB_DT_ENDPOINT && size >= 7 && in_storage && (data[offset + 3] & 3) == 2)
        {
            /* Transfer type 2 = bulk. */
            if (data[offset + 2] & 0x80)
            {
                found.bulk_in = data[offset + 2];
                found.in_packet_size = le16(data + offset + 4);
            }
            else
            {
                found.bulk_out = data[offset + 2];
                found.out_packet_size = le16(data + offset + 4);
            }

            if (found.bulk_in != 0 && found.bulk_out != 0)
            {
                *out = found;

                return 0;
            }
        }

        offset += size;
    }

    return -1;
}

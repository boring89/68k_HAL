/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mouse_packet.h"

#define PACKET_ALWAYS_ONE 0x08
#define PACKET_X_SIGN     0x10
#define PACKET_Y_SIGN     0x20
#define PACKET_X_OVERFLOW 0x40
#define PACKET_Y_OVERFLOW 0x80

#define MOUSE_MAX_MOVEMENT 255

/* 9-bit two's complement: the sign bit is in the flag byte. */
static int movement(uint8_t low, bool negative, bool overflow)
{
    if (overflow)
        return negative ? -MOUSE_MAX_MOVEMENT : MOUSE_MAX_MOVEMENT;

    return negative ? (int)low - 256 : (int)low;
}

void mouse_decoder_init(mouse_decoder_t *decoder)
{
    decoder->count = 0;
}

bool mouse_decoder_feed(mouse_decoder_t *decoder, uint8_t byte,
                        mouse_packet_t *packet)
{
    /* Not the start of a packet: drop the byte and wait for one. */
    if (decoder->count == 0 && !(byte & PACKET_ALWAYS_ONE))
        return false;

    decoder->bytes[decoder->count++] = byte;

    if (decoder->count < 3)
        return false;

    decoder->count = 0;

    uint8_t flags = decoder->bytes[0];

    packet->dx = movement(decoder->bytes[1], flags & PACKET_X_SIGN,
                          flags & PACKET_X_OVERFLOW);
    packet->dy = -movement(decoder->bytes[2], flags & PACKET_Y_SIGN,
                           flags & PACKET_Y_OVERFLOW);
    packet->buttons = flags & (MOUSE_BUTTON_LEFT | MOUSE_BUTTON_RIGHT |
                               MOUSE_BUTTON_MIDDLE);

    return true;
}

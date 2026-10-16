/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DRIVERS_PS2_MOUSE_MOUSE_PACKET_H
#define DRIVERS_PS2_MOUSE_MOUSE_PACKET_H

#include <stdint.h>
#include <stdbool.h>

/* Button bits, the same as in the first packet byte. */
#define MOUSE_BUTTON_LEFT   0x01
#define MOUSE_BUTTON_RIGHT  0x02
#define MOUSE_BUTTON_MIDDLE 0x04

/*
 * One decoded movement report. dx is positive to the right and dy positive
 * downwards (screen coordinates; the mouse itself reports "up" as
 * positive, so dy is negated). An overflow in the packet gives the largest
 * value of that direction (+-255) instead of a wrong one.
 */
typedef struct
{
    int dx;
    int dy;
    uint8_t buttons; /* MOUSE_BUTTON_* */
} mouse_packet_t;

/*
 * Assembles the standard 3-byte PS/2 mouse packets from the byte stream
 * (OSDev Wiki "PS/2 Mouse"):
 *
 *   byte 0: Y overflow | X overflow | Y sign | X sign | 1 | middle | right | left
 *   byte 1: X movement, low 8 bits (bit 8 is the X sign)
 *   byte 2: Y movement, low 8 bits (bit 8 is the Y sign)
 *
 * Bit 3 of byte 0 is always 1, which lets the decoder find the start of a
 * packet again after a lost byte.
 */
typedef struct
{
    uint8_t bytes[3];
    uint8_t count;
} mouse_decoder_t;

void mouse_decoder_init(mouse_decoder_t *decoder);

/* Feed one byte. Returns true and fills *packet when it completes a packet. */
bool mouse_decoder_feed(mouse_decoder_t *decoder, uint8_t byte,
                        mouse_packet_t *packet);

#endif

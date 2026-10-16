/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "scancode.h"

/*
 * Scan code set 1 (OSDev Wiki "PS/2 Keyboard"): a key sends one byte when
 * pressed and the same byte with bit 7 set when released. "Extended" keys
 * send 0xE0 first. The Pause key sends 0xE1 and five more bytes with no
 * release; those are skipped. The keyboard's own replies (0xFA acknowledge,
 * 0xFE resend, 0x00 and 0xFF errors) are not keys. (0xAA, the self test
 * result, is also the release of the left Shift key: it is a key.)
 */
static bool pending_e0;
static unsigned int skip_bytes;

void scancode_init(void)
{
    pending_e0 = false;
    skip_bytes = 0;
}

bool scancode_process(
    uint8_t scancode,
    keyboard_key_event_t *event
)
{
    if (skip_bytes > 0)
    {
        skip_bytes--;

        return false;
    }

    if (scancode == 0xE0)
    {
        pending_e0 = true;

        return false;
    }

    if (scancode == 0xE1)
    {
        skip_bytes = 5;

        return false;
    }

    if (scancode == 0xFA || scancode == 0xFE || scancode == 0x00 || scancode == 0xFF)
    {
        pending_e0 = false;

        return false;
    }

    event->code = scancode & 0x7F;
    event->pressed = (scancode & 0x80) == 0;
    event->extended = pending_e0;
    pending_e0 = false;

    return true;
}

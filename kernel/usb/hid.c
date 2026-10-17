/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hid.h"

/* Modifier bit n of byte 0 -> key (HID Usage Tables 1.12, table 12: usages 0xE0-0xE7 in this order). */
static const keycode_t modifier_keys[8] = {
    KEY_LEFT_CTRL, KEY_LEFT_SHIFT, KEY_LEFT_ALT, KEY_LEFT_GUI,
    KEY_RIGHT_CTRL, KEY_RIGHT_SHIFT, KEY_RIGHT_ALT, KEY_RIGHT_GUI,
};

keycode_t usb_hid_key(uint8_t usage)
{
    /* Usages 0x04-0x1D are the letters A-Z, 0x1E-0x27 the digits 1-9 and 0 (Usage Tables, table 12). */
    if (usage >= 0x04 && usage <= 0x1D)
        return (keycode_t)(KEY_A + (usage - 0x04));

    if (usage >= 0x1E && usage <= 0x26)
        return (keycode_t)(KEY_1 + (usage - 0x1E));

    if (usage == 0x27)
        return KEY_0;

    /* F1-F12: 0x3A-0x45. */
    if (usage >= 0x3A && usage <= 0x45)
        return (keycode_t)(KEY_F1 + (usage - 0x3A));

    switch (usage)
    {
    case 0x28: return KEY_ENTER;
    case 0x29: return KEY_ESC;
    case 0x2A: return KEY_BACKSPACE;
    case 0x2B: return KEY_TAB;
    case 0x2C: return KEY_SPACE;
    case 0x2D: return KEY_MINUS;
    case 0x2E: return KEY_EQUAL;
    case 0x2F: return KEY_LEFT_BRACKET;
    case 0x30: return KEY_RIGHT_BRACKET;
    case 0x31: return KEY_BACKSLASH;
    case 0x33: return KEY_SEMICOLON;
    case 0x34: return KEY_APOSTROPHE;
    case 0x35: return KEY_GRAVE;
    case 0x36: return KEY_COMMA;
    case 0x37: return KEY_PERIOD;
    case 0x38: return KEY_SLASH;
    case 0x39: return KEY_CAPS_LOCK;
    case 0x49: return KEY_INSERT;
    case 0x4A: return KEY_HOME;
    case 0x4B: return KEY_PAGE_UP;
    case 0x4C: return KEY_DELETE;
    case 0x4D: return KEY_END;
    case 0x4E: return KEY_PAGE_DOWN;
    case 0x4F: return KEY_RIGHT;
    case 0x50: return KEY_LEFT;
    case 0x51: return KEY_DOWN;
    case 0x52: return KEY_UP;
    default: return KEY_NONE;
    }
}

static int is_held(const uint8_t keys[6], uint8_t usage)
{
    for (int i = 0; i < 6; i++)
    {
        if (keys[i] == usage)
            return 1;
    }

    return 0;
}

int usb_hid_keyboard_report(usb_hid_keyboard_t *previous, const uint8_t *report, unsigned length,
                            input_event_t *events)
{
    int count = 0;

    if (length < USB_HID_KEYBOARD_REPORT_SIZE)
        return 0;

    /* Usage 1 in every slot is "ErrorRollOver": too many keys are down, the report says nothing. */
    for (int i = 0; i < 6; i++)
    {
        if (report[2 + i] == 0x01)
            return 0;
    }

    const uint8_t *keys = report + 2;

    /* Keys that are no longer down. */
    for (int i = 0; i < 6; i++)
    {
        uint8_t usage = previous->keys[i];

        if (usage > 3 && !is_held(keys, usage) && usb_hid_key(usage) != KEY_NONE)
            events[count++] = input_event_keyboard(usb_hid_key(usage), false);
    }

    /* Modifiers that changed: let go of first, then pressed. */
    uint8_t changed = (uint8_t)(previous->modifiers ^ report[0]);

    for (int bit = 0; bit < 8; bit++)
    {
        if ((changed & (1u << bit)) && !(report[0] & (1u << bit)))
            events[count++] = input_event_keyboard(modifier_keys[bit], false);
    }

    for (int bit = 0; bit < 8; bit++)
    {
        if ((changed & (1u << bit)) && (report[0] & (1u << bit)))
            events[count++] = input_event_keyboard(modifier_keys[bit], true);
    }

    /* Keys that are newly down. */
    for (int i = 0; i < 6; i++)
    {
        uint8_t usage = keys[i];

        if (usage > 3 && !is_held(previous->keys, usage) && usb_hid_key(usage) != KEY_NONE)
            events[count++] = input_event_keyboard(usb_hid_key(usage), true);
    }

    previous->modifiers = report[0];

    for (int i = 0; i < 6; i++)
        previous->keys[i] = keys[i];

    return count;
}

int usb_hid_mouse_report(const uint8_t *report, unsigned length, input_event_t *event)
{
    if (length < 3)
        return -1;

    *event = input_event_mouse((int8_t)report[1], (int8_t)report[2], report[0] & 7u);

    return 0;
}

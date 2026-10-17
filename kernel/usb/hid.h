/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USB_HID_H
#define KERNEL_USB_HID_H

#include "kernel/input/event.h"

#include <stdint.h>

/*
 * USB HID "boot protocol" reports (Device Class Definition for HID 1.11, appendix B; Universal
 * Serial Bus HID Usage Tables 1.12, chapter 10): the fixed, simple report format every
 * keyboard and mouse can be switched to, so no report descriptor has to be parsed.
 *
 *   keyboard, 8 bytes:  [0] modifier bits (LCtrl 0, LShift 1, LAlt 2, LGUI 3, RCtrl 4,
 *                       RShift 5, RAlt 6, RGUI 7)  [1] reserved  [2..7] up to six usage codes
 *                       of the keys held down (0 = none, 1 = too many keys held)
 *   mouse, 3+ bytes:    [0] buttons (bit 0 left, 1 right, 2 middle)  [1] dx  [2] dy (signed)
 *                       [3] wheel (optional, ignored)
 */

#define USB_HID_KEYBOARD_REPORT_SIZE 8

/* At most this many events come out of one keyboard report: 8 modifiers, 6 releases, 6 presses. */
#define USB_HID_MAX_EVENTS 20

/* What the last keyboard report said, to find out what changed in the next one. */
typedef struct
{
    uint8_t modifiers;
    uint8_t keys[6];
} usb_hid_keyboard_t;

/* The key code of a usage code of the keyboard page, or KEY_NONE if we have no such key. */
keycode_t usb_hid_key(uint8_t usage);

/*
 * Compare a keyboard report with the one before (previous, updated) and write the key events
 * that happened into events (at least USB_HID_MAX_EVENTS entries): releases first, then
 * modifier changes, then presses, so that a chord like Ctrl+C arrives as Ctrl down, C down.
 * A report that is shorter than 8 bytes or says "too many keys" changes nothing. Returns the
 * number of events.
 */
int usb_hid_keyboard_report(usb_hid_keyboard_t *previous, const uint8_t *report, unsigned length,
                            input_event_t *events);

/* A mouse report as a mouse event. Returns 0, or -1 if the report is shorter than 3 bytes. */
int usb_hid_mouse_report(const uint8_t *report, unsigned length, input_event_t *event);

#endif

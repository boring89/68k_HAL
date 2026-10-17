/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/usb/hid.h"

#include <string.h>

static void test_usage_mapping(void)
{
    TEST_ASSERT_EQ_INT(KEY_A, usb_hid_key(0x04));
    TEST_ASSERT_EQ_INT(KEY_Z, usb_hid_key(0x1D));
    TEST_ASSERT_EQ_INT(KEY_1, usb_hid_key(0x1E));
    TEST_ASSERT_EQ_INT(KEY_9, usb_hid_key(0x26));
    TEST_ASSERT_EQ_INT(KEY_0, usb_hid_key(0x27));
    TEST_ASSERT_EQ_INT(KEY_ENTER, usb_hid_key(0x28));
    TEST_ASSERT_EQ_INT(KEY_SPACE, usb_hid_key(0x2C));
    TEST_ASSERT_EQ_INT(KEY_SEMICOLON, usb_hid_key(0x33));
    TEST_ASSERT_EQ_INT(KEY_F1, usb_hid_key(0x3A));
    TEST_ASSERT_EQ_INT(KEY_F12, usb_hid_key(0x45));
    TEST_ASSERT_EQ_INT(KEY_RIGHT, usb_hid_key(0x4F));
    TEST_ASSERT_EQ_INT(KEY_UP, usb_hid_key(0x52));
    TEST_ASSERT_EQ_INT(KEY_NONE, usb_hid_key(0x00));
    TEST_ASSERT_EQ_INT(KEY_NONE, usb_hid_key(0x65)); /* Application: not a key we have */
    TEST_ASSERT_EQ_INT(KEY_NONE, usb_hid_key(0xFF));
}

static void test_press_and_release(void)
{
    usb_hid_keyboard_t state;
    input_event_t events[USB_HID_MAX_EVENTS];
    uint8_t report[8] = {0, 0, 0x04, 0, 0, 0, 0, 0}; /* A down */

    memset(&state, 0, sizeof(state));

    TEST_ASSERT_EQ_INT(1, usb_hid_keyboard_report(&state, report, 8, events));
    TEST_ASSERT_EQ_INT(INPUT_EVENT_KEYBOARD, events[0].type);
    TEST_ASSERT_EQ_INT(KEY_A, events[0].data.keyboard.key);
    TEST_ASSERT(events[0].data.keyboard.pressed);

    /* The same report again: nothing happens (the device repeats it every poll). */
    TEST_ASSERT_EQ_INT(0, usb_hid_keyboard_report(&state, report, 8, events));

    /* B joins A, then A goes. */
    report[3] = 0x05;
    TEST_ASSERT_EQ_INT(1, usb_hid_keyboard_report(&state, report, 8, events));
    TEST_ASSERT_EQ_INT(KEY_B, events[0].data.keyboard.key);

    report[2] = 0;
    TEST_ASSERT_EQ_INT(1, usb_hid_keyboard_report(&state, report, 8, events));
    TEST_ASSERT_EQ_INT(KEY_A, events[0].data.keyboard.key);
    TEST_ASSERT(!events[0].data.keyboard.pressed);

    memset(report, 0, 8);
    TEST_ASSERT_EQ_INT(1, usb_hid_keyboard_report(&state, report, 8, events));
    TEST_ASSERT_EQ_INT(KEY_B, events[0].data.keyboard.key);
    TEST_ASSERT(!events[0].data.keyboard.pressed);
}

static void test_modifier_order(void)
{
    usb_hid_keyboard_t state;
    input_event_t events[USB_HID_MAX_EVENTS];
    uint8_t report[8] = {0x01, 0, 0x06, 0, 0, 0, 0, 0}; /* Ctrl and C at once */

    memset(&state, 0, sizeof(state));

    /* The modifier is down before the key: Ctrl, then C. */
    TEST_ASSERT_EQ_INT(2, usb_hid_keyboard_report(&state, report, 8, events));
    TEST_ASSERT_EQ_INT(KEY_LEFT_CTRL, events[0].data.keyboard.key);
    TEST_ASSERT(events[0].data.keyboard.pressed);
    TEST_ASSERT_EQ_INT(KEY_C, events[1].data.keyboard.key);

    /* Both let go at once: C comes up before Ctrl. */
    memset(report, 0, 8);
    TEST_ASSERT_EQ_INT(2, usb_hid_keyboard_report(&state, report, 8, events));
    TEST_ASSERT_EQ_INT(KEY_C, events[0].data.keyboard.key);
    TEST_ASSERT(!events[0].data.keyboard.pressed);
    TEST_ASSERT_EQ_INT(KEY_LEFT_CTRL, events[1].data.keyboard.key);
    TEST_ASSERT(!events[1].data.keyboard.pressed);

    /* Right shift (bit 5) and right GUI (bit 7). */
    report[0] = 0xA0;
    TEST_ASSERT_EQ_INT(2, usb_hid_keyboard_report(&state, report, 8, events));
    TEST_ASSERT_EQ_INT(KEY_RIGHT_SHIFT, events[0].data.keyboard.key);
    TEST_ASSERT_EQ_INT(KEY_RIGHT_GUI, events[1].data.keyboard.key);
}

static void test_rollover_and_short_reports(void)
{
    usb_hid_keyboard_t state;
    input_event_t events[USB_HID_MAX_EVENTS];
    uint8_t held[8] = {0, 0, 0x04, 0, 0, 0, 0, 0};
    uint8_t rollover[8] = {0, 0, 1, 1, 1, 1, 1, 1};

    memset(&state, 0, sizeof(state));
    usb_hid_keyboard_report(&state, held, 8, events);

    /* "Too many keys" and truncated reports change nothing; the next good report is compared with the last good one. */
    TEST_ASSERT_EQ_INT(0, usb_hid_keyboard_report(&state, rollover, 8, events));
    TEST_ASSERT_EQ_INT(0, usb_hid_keyboard_report(&state, held, 4, events));
    TEST_ASSERT_EQ_INT(0, usb_hid_keyboard_report(&state, held, 8, events));

    /* Six keys at once give six events; a key without a mapping gives none. */
    uint8_t six[8] = {0, 0, 0x04, 0x05, 0x06, 0x07, 0x65, 0x08};

    TEST_ASSERT_EQ_INT(4, usb_hid_keyboard_report(&state, six, 8, events)); /* A was held already, 0x65 has no key */
}

static void test_mouse(void)
{
    input_event_t event;
    uint8_t report[4] = {0x05, 0x03, 0xFE, 0x01}; /* left+middle, dx +3, dy -2, wheel */

    TEST_ASSERT_EQ_INT(0, usb_hid_mouse_report(report, 4, &event));
    TEST_ASSERT_EQ_INT(INPUT_EVENT_MOUSE, event.type);
    TEST_ASSERT_EQ_INT(3, event.data.mouse.dx);
    TEST_ASSERT_EQ_INT(-2, event.data.mouse.dy);
    TEST_ASSERT_EQ_INT(5, event.data.mouse.buttons);

    TEST_ASSERT_EQ_INT(0, usb_hid_mouse_report(report, 3, &event));
    TEST_ASSERT_EQ_INT(-1, usb_hid_mouse_report(report, 2, &event));

    report[1] = 0x80; /* -128 */
    usb_hid_mouse_report(report, 3, &event);
    TEST_ASSERT_EQ_INT(-128, event.data.mouse.dx);
}

int main(void)
{
    TEST_RUN(test_usage_mapping);
    TEST_RUN(test_press_and_release);
    TEST_RUN(test_modifier_order);
    TEST_RUN(test_rollover_and_short_reports);
    TEST_RUN(test_mouse);

    return TEST_REPORT("usb_hid");
}

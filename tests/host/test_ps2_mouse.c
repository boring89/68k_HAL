/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "drivers/ps2_mouse/mouse_packet.h"
#include "kernel/input/event.h"

static bool feed3(mouse_decoder_t *d, uint8_t b0, uint8_t b1, uint8_t b2,
                  mouse_packet_t *p)
{
    bool first = mouse_decoder_feed(d, b0, p);
    bool second = mouse_decoder_feed(d, b1, p);
    bool third = mouse_decoder_feed(d, b2, p);

    TEST_ASSERT(!first);
    TEST_ASSERT(!second);
    return third;
}

static void test_positive_movement_and_buttons(void)
{
    mouse_decoder_t d;
    mouse_packet_t p;

    mouse_decoder_init(&d);

    /* Left + middle held, 5 right, 3 up (the mouse reports up as positive). */
    TEST_ASSERT(feed3(&d, 0x08 | 0x01 | 0x04, 5, 3, &p));
    TEST_ASSERT_EQ_INT(5, p.dx);
    TEST_ASSERT_EQ_INT(-3, p.dy);
    TEST_ASSERT_EQ_INT(MOUSE_BUTTON_LEFT | MOUSE_BUTTON_MIDDLE, p.buttons);

    TEST_ASSERT(feed3(&d, 0x08 | 0x02, 0, 0, &p));
    TEST_ASSERT_EQ_INT(0, p.dx);
    TEST_ASSERT_EQ_INT(0, p.dy);
    TEST_ASSERT_EQ_INT(MOUSE_BUTTON_RIGHT, p.buttons);
}

static void test_negative_movement(void)
{
    mouse_decoder_t d;
    mouse_packet_t p;

    mouse_decoder_init(&d);

    /* Both sign bits set: 0x1FF - ... = 9-bit two's complement. 0xFF = -1. */
    TEST_ASSERT(feed3(&d, 0x08 | 0x10 | 0x20, 0xFF, 0xFE, &p));
    TEST_ASSERT_EQ_INT(-1, p.dx);
    TEST_ASSERT_EQ_INT(2, p.dy);       /* mouse Y -2 = 2 down */
    TEST_ASSERT_EQ_INT(0, p.buttons);

    /* -256 is the most negative 9-bit value: sign bit set, low byte 0. */
    TEST_ASSERT(feed3(&d, 0x08 | 0x10, 0x00, 0x00, &p));
    TEST_ASSERT_EQ_INT(-256, p.dx);
}

static void test_overflow_saturates(void)
{
    mouse_decoder_t d;
    mouse_packet_t p;

    mouse_decoder_init(&d);

    /* X overflow to the right, Y overflow downwards (mouse: negative). */
    TEST_ASSERT(feed3(&d, 0x08 | 0x40 | 0x80 | 0x20, 0x12, 0x34, &p));
    TEST_ASSERT_EQ_INT(255, p.dx);
    TEST_ASSERT_EQ_INT(255, p.dy);

    /* X overflow to the left. */
    TEST_ASSERT(feed3(&d, 0x08 | 0x40 | 0x10, 0x12, 0x00, &p));
    TEST_ASSERT_EQ_INT(-255, p.dx);
}

/* A lost byte shifts the stream; bit 3 of the first byte brings it back. */
static void test_resync(void)
{
    mouse_decoder_t d;
    mouse_packet_t p;

    mouse_decoder_init(&d);

    /* Leading garbage without bit 3 is dropped. */
    TEST_ASSERT(!mouse_decoder_feed(&d, 0x00, &p));
    TEST_ASSERT(!mouse_decoder_feed(&d, 0x07, &p));
    TEST_ASSERT(feed3(&d, 0x08, 1, 2, &p));
    TEST_ASSERT_EQ_INT(1, p.dx);
    TEST_ASSERT_EQ_INT(-2, p.dy);
}

/* Seen in QEMU: the ACK of "enable reporting" arrived as the first IRQ 12 byte. */
static void test_late_ack_dropped(void)
{
    mouse_decoder_t d;
    mouse_packet_t p;

    mouse_decoder_init(&d);

    TEST_ASSERT(!mouse_decoder_feed(&d, 0xFA, &p));
    TEST_ASSERT(!mouse_decoder_feed(&d, 0xFE, &p));

    /* mouse_move 5 5 in QEMU: right 5, down 5 (PS/2 Y is up-positive). */
    TEST_ASSERT(feed3(&d, 0x28, 0x05, 0xFB, &p));
    TEST_ASSERT_EQ_INT(5, p.dx);
    TEST_ASSERT_EQ_INT(5, p.dy);
    TEST_ASSERT_EQ_INT(0, p.buttons);
}

static void test_event_constructor(void)
{
    input_event_t e = input_event_mouse(-300, 7, MOUSE_BUTTON_RIGHT);

    TEST_ASSERT_EQ_INT(INPUT_EVENT_MOUSE, e.type);
    TEST_ASSERT_EQ_INT(-300, e.data.mouse.dx);
    TEST_ASSERT_EQ_INT(7, e.data.mouse.dy);
    TEST_ASSERT_EQ_INT(MOUSE_BUTTON_RIGHT, e.data.mouse.buttons);
}

int main(void)
{
    TEST_RUN(test_positive_movement_and_buttons);
    TEST_RUN(test_negative_movement);
    TEST_RUN(test_overflow_saturates);
    TEST_RUN(test_resync);
    TEST_RUN(test_late_ack_dropped);
    TEST_RUN(test_event_constructor);

    return TEST_REPORT("ps2_mouse");
}

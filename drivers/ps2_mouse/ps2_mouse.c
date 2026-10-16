/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ps2_mouse.h"
#include "mouse_packet.h"

#include "kernel/input/event.h"
#include "kernel/input/event_buffer.h"

#include "hal/irq.h"

#include "arch/x86/io/io.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * 8042 controller ports and commands (OSDev Wiki "8042 PS/2 Controller" and
 * "PS/2 Mouse").
 */
#define PS2_DATA_PORT 0x60
#define PS2_STATUS_PORT 0x64  /* read */
#define PS2_COMMAND_PORT 0x64 /* write */

#define PS2_STATUS_OUTPUT_FULL 0x01 /* a byte is waiting at the data port */
#define PS2_STATUS_INPUT_FULL 0x02  /* the controller is not ready for a byte */

#define PS2_CMD_READ_CONFIG 0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_ENABLE_AUX 0xA8
#define PS2_CMD_WRITE_AUX 0xD4 /* send the next data byte to the mouse */

#define PS2_CONFIG_AUX_IRQ 0x02      /* bit 1: IRQ 12 enabled */
#define PS2_CONFIG_AUX_CLOCK_OFF 0x20 /* bit 5: second channel disabled */

#define MOUSE_CMD_DEFAULTS 0xF6
#define MOUSE_CMD_ENABLE_REPORTING 0xF4
#define MOUSE_ACK 0xFA

#define PS2_MOUSE_IRQ 12

/* Polling limit; a working controller answers within a few iterations. */
#define PS2_WAIT_LOOPS 100000

static mouse_decoder_t decoder;

static bool wait_input_ready(void)
{
    for (int i = 0; i < PS2_WAIT_LOOPS; i++)
    {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL))
            return true;
    }
    return false;
}

static bool wait_output_ready(void)
{
    for (int i = 0; i < PS2_WAIT_LOOPS; i++)
    {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL)
            return true;
    }
    return false;
}

static bool write_command(uint8_t command)
{
    if (!wait_input_ready())
        return false;
    outb(PS2_COMMAND_PORT, command);
    return true;
}

static bool write_data(uint8_t data)
{
    if (!wait_input_ready())
        return false;
    outb(PS2_DATA_PORT, data);
    return true;
}

/* Send a command byte to the mouse and wait for its ACK. */
static bool mouse_command(uint8_t command)
{
    if (!write_command(PS2_CMD_WRITE_AUX) || !write_data(command))
        return false;

    return wait_output_ready() && inb(PS2_DATA_PORT) == MOUSE_ACK;
}

static void ps2_mouse_handler(void)
{
    mouse_packet_t packet;

    if (mouse_decoder_feed(&decoder, inb(PS2_DATA_PORT), &packet))
    {
        input_event_t event =
            input_event_mouse(packet.dx, packet.dy, packet.buttons);

        input_event_push(&event);
    }
}

int ps2_mouse_init(void)
{
    mouse_decoder_init(&decoder);

    /* Enable the second channel, then its interrupt and clock in the config. */
    if (!write_command(PS2_CMD_ENABLE_AUX) ||
        !write_command(PS2_CMD_READ_CONFIG) || !wait_output_ready())
        return -1;

    uint8_t config = inb(PS2_DATA_PORT);

    config |= PS2_CONFIG_AUX_IRQ;
    config &= (uint8_t)~PS2_CONFIG_AUX_CLOCK_OFF;

    if (!write_command(PS2_CMD_WRITE_CONFIG) || !write_data(config))
        return -1;

    if (!mouse_command(MOUSE_CMD_DEFAULTS) ||
        !mouse_command(MOUSE_CMD_ENABLE_REPORTING))
        return -1;

    /* Throw away anything still queued (e.g. a late ACK) before IRQ 12 runs. */
    for (int i = 0; i < 16 && (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL); i++)
        (void)inb(PS2_DATA_PORT);

    if (hal_irq_register(PS2_MOUSE_IRQ, ps2_mouse_handler) != 0)
        return -1;

    return hal_irq_unmask(PS2_MOUSE_IRQ);
}

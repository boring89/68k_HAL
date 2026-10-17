/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "inputdev.h"

#include "process.h"
#include "sched.h"
#include "signal.h"

#include "kernel/input/event.h"
#include "kernel/input/event_buffer.h"
#include "kernel/input/linux_keys.h"
#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"

#include "hal/cpu.h"
#include "hal/timer.h"

#define EV_SYN 0
#define EV_KEY 1
#define EV_REL 2
#define REL_X 0
#define REL_Y 1
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112

#define QUEUE_SIZE 64
#define EVENT_SIZE 16
/* The most events one kernel event can turn into: dx, dy, three buttons, sync. */
#define MAX_PER_EVENT 6

typedef struct
{
    uint32_t seconds;
    uint32_t microseconds;
    uint16_t type;
    uint16_t code;
    int32_t value;
} linux_event_t;

static linux_event_t queue[QUEUE_SIZE];
static uint32_t head;
static uint32_t count;
static uint8_t last_buttons;

static void add(uint16_t type, uint16_t code, int32_t value)
{
    uint64_t ns = hal_timer_ns();
    linux_event_t *event = &queue[(head + count++) % QUEUE_SIZE];

    event->seconds = (uint32_t)(ns / 1000000000u);
    event->microseconds = (uint32_t)(ns % 1000000000u / 1000u);
    event->type = type;
    event->code = code;
    event->value = value;
}

static void convert(const input_event_t *event)
{
    if (event->type == INPUT_EVENT_KEYBOARD)
    {
        int code = input_linux_key_code(event->data.keyboard.key);

        if (code == 0)
            return;

        add(EV_KEY, (uint16_t)code, event->data.keyboard.pressed ? 1 : 0);
        add(EV_SYN, 0, 0);
    }
    else if (event->type == INPUT_EVENT_MOUSE)
    {
        static const uint16_t buttons[3] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE};
        uint8_t changed = event->data.mouse.buttons ^ last_buttons;

        if (event->data.mouse.dx != 0)
            add(EV_REL, REL_X, event->data.mouse.dx);

        if (event->data.mouse.dy != 0)
            add(EV_REL, REL_Y, event->data.mouse.dy);

        for (int i = 0; i < 3; i++)
        {
            if (changed & (1u << i))
                add(EV_KEY, buttons[i], (event->data.mouse.buttons >> i) & 1);
        }

        last_buttons = event->data.mouse.buttons;
        add(EV_SYN, 0, 0);
    }
}

/* Move the events the drivers have queued into the Linux-format queue, as far as there is room. */
static void fill(void)
{
    input_event_t event;

    while (count + MAX_PER_EVENT <= QUEUE_SIZE && input_event_pop(&event) == 0)
        convert(&event);
}

static int open_count;

int inputdev_is_open(void)
{
    return open_count > 0;
}

static void inputdev_close(vfs_file_t *file)
{
    (void)file;
    open_count--;
}

void inputdev_flush(void)
{
    input_event_t event;

    open_count++; /* called when the device is opened */

    head = 0;
    count = 0;

    while (input_event_pop(&event) == 0)
    {
        if (event.type == INPUT_EVENT_MOUSE)
            last_buttons = event.data.mouse.buttons;
    }
}

static int32_t inputdev_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t size)
{
    (void)pos;

    if (size < EVENT_SIZE)
        return -K_EINVAL;

    for (;;)
    {
        fill();

        if (count > 0)
            break;

        if (file->flags & 04000u) /* O_NONBLOCK */
            return -K_EAGAIN;

        if (signal_interrupts(user_current))
            return -K_EINTR;

        sched_wait();
    }

    uint32_t n = size / EVENT_SIZE;
    uint8_t *out = buffer;

    if (n > count)
        n = count;

    for (uint32_t i = 0; i < n; i++)
    {
        k_memcpy(out + i * EVENT_SIZE, &queue[head], EVENT_SIZE);
        head = (head + 1) % QUEUE_SIZE;
    }

    count -= n;

    return (int32_t)(n * EVENT_SIZE);
}

static uint32_t inputdev_poll(vfs_file_t *file, uint32_t events)
{
    (void)file;
    fill();

    return (events & VFS_POLLIN) && count > 0 ? VFS_POLLIN : 0;
}

static int32_t inputdev_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    (void)file;
    stat->mode = VFS_S_IFCHR | 0666;
    stat->size = 0;
    stat->inode = 5;
    stat->mtime = 0;

    return 0;
}

const vfs_file_ops_t inputdev_ops = {
    .read = inputdev_read,
    .stat = inputdev_stat,
    .poll = inputdev_poll,
    .close = inputdev_close,
};

/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_LIB_RING_BUFFER_H
#define KERNEL_LIB_RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>


typedef struct
{
    void *data;

    uint32_t element_size;
    uint32_t capacity;

    uint32_t read_pos;
    uint32_t write_pos;

} ring_buffer_t;


void ring_buffer_init(
    ring_buffer_t *buffer,
    void *storage,
    uint32_t element_size,
    uint32_t capacity
);


bool ring_buffer_has_data(
    ring_buffer_t *buffer
);


bool ring_buffer_is_full(
    ring_buffer_t *buffer
);


int ring_buffer_push(
    ring_buffer_t *buffer,
    void *data
);


int ring_buffer_pop(
    ring_buffer_t *buffer,
    void *data
);


#endif
/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ring_buffer.h"

#include "../lib/k_memory.h"

void ring_buffer_init(
    ring_buffer_t *buffer,
    void *storage,
    uint32_t element_size,
    uint32_t capacity
)
{
    buffer->data = storage;

    buffer->element_size = element_size;
    buffer->capacity = capacity;

    buffer->read_pos = 0;
    buffer->write_pos = 0;
}


bool ring_buffer_has_data(ring_buffer_t *buffer)
{
    return buffer->read_pos != buffer->write_pos;
}


bool ring_buffer_is_full(ring_buffer_t *buffer)
{
    uint32_t next =
        (buffer->write_pos + 1) % buffer->capacity;

    return next == buffer->read_pos;
}


int ring_buffer_push(
    ring_buffer_t *buffer,
    void *data
)
{
    if(ring_buffer_is_full(buffer))
        return -1;


    uint8_t *dest =
        (uint8_t *)buffer->data +
        buffer->write_pos * buffer->element_size;


    k_memcpy(
        dest,
        data,
        buffer->element_size
    );


    buffer->write_pos++;

    if(buffer->write_pos >= buffer->capacity)
        buffer->write_pos = 0;


    return 0;
}


int ring_buffer_pop(
    ring_buffer_t *buffer,
    void *data
)
{
    if(!ring_buffer_has_data(buffer))
        return -1;


    uint8_t *src =
        (uint8_t *)buffer->data +
        buffer->read_pos * buffer->element_size;


    k_memcpy(
        data,
        src,
        buffer->element_size
    );


    buffer->read_pos++;

    if(buffer->read_pos >= buffer->capacity)
        buffer->read_pos = 0;


    return 0;
}
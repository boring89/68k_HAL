#include "buffer.h"


void input_buffer_init(input_buffer_t *buffer)
{
    buffer->read_pos = 0;
    buffer->write_pos = 0;
}


bool input_buffer_has_data(input_buffer_t *buffer)
{
    return buffer->read_pos != buffer->write_pos;
}


bool input_buffer_is_full(input_buffer_t *buffer)
{
    uint32_t next =
        (buffer->write_pos + 1) % INPUT_BUFFER_SIZE;

    return next == buffer->read_pos;
}


int input_buffer_push(input_buffer_t *buffer, uint8_t data)
{
    if (input_buffer_is_full(buffer))
    {
        return -1;
    }


    buffer->data[buffer->write_pos] = data;

    buffer->write_pos =
        (buffer->write_pos + 1) % INPUT_BUFFER_SIZE;


    return 0;
}


int input_buffer_pop(input_buffer_t *buffer, uint8_t *data)
{
    if (!input_buffer_has_data(buffer))
    {
        return -1;
    }


    *data = buffer->data[buffer->read_pos];

    buffer->read_pos =
        (buffer->read_pos + 1) % INPUT_BUFFER_SIZE;


    return 0;
}
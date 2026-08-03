#ifndef KERNEL_INPUT_BUFFER_H
#define KERNEL_INPUT_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

#define INPUT_BUFFER_SIZE 128

typedef struct
{
    uint8_t data[INPUT_BUFFER_SIZE];

    uint32_t read_pos;
    uint32_t write_pos;

} input_buffer_t;


void input_buffer_init(input_buffer_t *buffer);

bool input_buffer_has_data(input_buffer_t *buffer);

bool input_buffer_is_full(input_buffer_t *buffer);

int input_buffer_push(input_buffer_t *buffer, uint8_t data);

int input_buffer_pop(input_buffer_t *buffer, uint8_t *data);


#endif
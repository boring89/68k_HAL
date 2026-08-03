#include "keyboard.h"

#include "../input/buffer.h"
#include "../../hal/io/io.h"


static input_buffer_t keyboard_buffer;


void keyboard_init(void)
{
    input_buffer_init(&keyboard_buffer);
}


void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);


    if (scancode & 0x80)
        return;


    input_buffer_push(
        &keyboard_buffer,
        scancode
    );
}


bool keyboard_has_key(void)
{
    return input_buffer_has_data(
        &keyboard_buffer
    );
}


int keyboard_read(uint8_t *scancode)
{
    return input_buffer_pop(
        &keyboard_buffer,
        scancode
    );
}
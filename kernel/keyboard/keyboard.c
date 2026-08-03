#include "keyboard.h"

#include "../../hal/io/io.h"

volatile uint8_t keyboard_last_scancode = 0;

void keyboard_handler(void)
{
    keyboard_last_scancode = inb(0x60);
}
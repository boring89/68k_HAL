#include "keyboard.h"

#include "../../hal/io/io.h"

volatile uint8_t keyboard_last_scancode = 0;

void keyboard_handler(void)
{
    uint8_t scancode = inb(0x60);

    // Ignore key release
    if (scancode & 0x80)
    {
        return;
    }

    keyboard_last_scancode = scancode;
}
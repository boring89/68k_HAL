#ifndef KERNEL_KEYBOARD_KEYBOARD_H
#define KERNEL_KEYBOARD_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>

void keyboard_init(void);

bool keyboard_has_key(void);

int keyboard_read(uint8_t *scancode);

void keyboard_handler(void);

#endif
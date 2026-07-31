#include "../../hal/vga/vga.h"
#include "../../hal/serial/serial.h"
#include "console.h"

void console_init(void)
{
    vga_init();
    serial_init();
}

void console_write(const char *str)
{
    vga_write(str);
    serial_write(str);
}

void console_write_line(const char *str)
{
    vga_write_line(str);
    serial_write_line(str);
}

void console_clear(void)
{
    vga_clear();
}
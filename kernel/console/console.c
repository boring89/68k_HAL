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

void console_write_char(char c)
{
    vga_put_char(c);
    serial_write_char(c);
}

void console_write_line(const char *str)
{
    vga_write_line(str);
    serial_write_line(str);
}

void console_write_color(const char *str, vga_color_t fg, vga_color_t bg)
{
    vga_write_color(str, fg, bg);
    serial_write(str);
}

void console_write_color_line(const char *str, vga_color_t fg, vga_color_t bg)
{
    vga_write_color_line(str, fg, bg);
    serial_write_line(str);
}

void console_clear(void)
{
    vga_clear();
}
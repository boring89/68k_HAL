#include "printf.h"

#include <stdarg.h>

#include "../console/console.h"
#include "string.h"

void kernel_vprintf(const char *fmt, va_list args)
{
    if (!fmt)
        return;

    while (*fmt)
    {
        if (*fmt != '%')
        {
            console_write_char(*fmt);
            fmt++;
            continue;
        }

        fmt++;

        switch (*fmt)
        {
        case '%':
            console_write_char('%');
            break;
        case 'c':
        {
            char c = va_arg(args, int);
            console_write_char(c);
            break;
        }

        case 's':
        {
            const char *str = va_arg(args, char *); 
            console_write(str);
            break;
        }

        case 'd':
        {
            // TODO
            break;
        }

        case 'u':
        {
            // TODO
            break;
        }

        case 'x':
        {
            // TODO
            break;
        }
        default:
            console_write_char('?') break;
        }

        fmt++;
    }
}

void kernel_printf(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);

    kernel_vprintf(fmt, args);

    va_end(args);
}
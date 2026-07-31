#ifndef KERNEL_PRINTF_H
#define KERNEL_PRINTF_H

#include <stdarg.h>

void kernel_printf(const char *fmt, ...);
void kernel_vprintf(const char *fmt, va_list args);

#endif
#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

static int serial_ready(void);

void serial_init(void);
int serial_write_char(char c);
void serial_write(const char* str);

#endif
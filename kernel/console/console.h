#ifndef CONSOLE_H
#define CONSOLE_H

void console_init(void);

void console_write(const char *str);
void console_write_line(const char *str);
void console_clear(void);

#endif
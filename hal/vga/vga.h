#ifndef VGA_H
#define VGA_H
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_DEFAULT_COLOR \
((VGA_BLACK << 4) | VGA_LIGHT_GREY)

typedef enum
{
    VGA_BLACK = 0x0,
    VGA_BLUE = 0x1,
    VGA_GREEN = 0x2,
    VGA_CYAN = 0x3,
    VGA_RED = 0x4,
    VGA_MAGENTA = 0x5,
    VGA_BROWN = 0x6,
    VGA_LIGHT_GREY = 0x7,

    VGA_DARK_GREY = 0x8,
    VGA_LIGHT_BLUE = 0x9,
    VGA_LIGHT_GREEN = 0xA,
    VGA_LIGHT_CYAN = 0xB,
    VGA_LIGHT_RED = 0xC,
    VGA_LIGHT_MAGENTA = 0xD,
    VGA_YELLOW = 0xE,
    VGA_WHITE = 0xF

} vga_color_t;

static inline int vga_index(void);
void vga_set_cursor(int x, int y);
void vga_set_color(vga_color_t foreground, vga_color_t background);
void vga_clear(void);
void vga_init(void);
void vga_newline(void);
void vga_put_char(char c);
void vga_write(const char *str);
void vga_scroll(void);
void vga_write_line(const char *str);
void vga_write_color(const char *str, vga_color_t fg, vga_color_t bg);

#endif
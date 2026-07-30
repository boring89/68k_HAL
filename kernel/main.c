#include "../hal/vga.h"


void kernel_main(void) 
{
	vga_clear();

	vga_write("Normal\n");

	vga_set_color(VGA_GREEN, VGA_GREEN);

	vga_write("Green text");

    while(1)
    {
        asm volatile("hlt");
    }
}

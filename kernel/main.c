#include "../hal/vga/vga.h"
#include "../hal/serial/serial.h"


void kernel_main(void) 
{
	vga_init();
    vga_write("A");
	// vga_write_line("Normal text OK");
	// vga_write_color_line("Colored text OK", VGA_GREEN, VGA_BLACK);

    // serial_init();
    // serial_write("Serial OK");

    while(1)
    {
        asm volatile("hlt");
    }
}

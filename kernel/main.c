#include "console/console.h"


void kernel_main(void) 
{
	console_init();

    console_write_line("kernel boot");

    while(1)
    {
        asm volatile("hlt");
    }
}

#include "console/console.h"
#include "log/log.h"
#include "interrupt/idt.h"

void kernel_main(void)
{
    console_init();

    kernel_log(LOG_INFO, "Kernel Booting...");

    idt_init();

    kernel_log(LOG_INFO, "IDT loaded");

    asm volatile("int $0x30");

    while (1)
    {
        asm volatile("hlt");
    }
}

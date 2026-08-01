#include "console/console.h"
#include "log/log.h"
#include "interrupt/idt.h"
#include "interrupt/pic.h"
#include "timer/pit.h"

#include <stdint.h>

void kernel_main(void)
{
    console_init();

    kernel_log(LOG_INFO, "Kernel Booting...");

    idt_init();
    kernel_log(LOG_INFO, "IDT loaded");

    pic_init();
    kernel_log(LOG_INFO, "PIC initialized");

    pit_init(100);

    asm volatile("sti");

    uint32_t last_tick = 0;

    while (1)
    {
        uint32_t tick = pit_get_ticks();

        if (tick - last_tick >= 100)
        {
            kernel_printf("1 second\n");
            last_tick = tick;
        }

        asm volatile("hlt");
    }
}

#include "console/console.h"
#include "log/log.h"


void kernel_main(void) 
{
	console_init();

    kernel_log(LOG_INFO, "Kernel Booting...");
    kernel_log(LOG_DEBUG, "Debug system online");
    kernel_log(LOG_WARN, "Warning test");
    kernel_log(LOG_ERROR, "Error test");
    kernel_log(LOG_FATAL, "Fatal test");

    while(1)
    {
        asm volatile("hlt");
    }
}

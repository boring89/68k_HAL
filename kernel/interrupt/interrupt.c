#include "interrupt.h"
#include "../log/log.h"

#include "interrupt_frame.h"

void interrupt_handler(struct interrupt_frame *frame)
{
    kernel_log(LOG_INFO, "interrupt received");
}
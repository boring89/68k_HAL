#include "interrupt.h"
#include "../log/log.h"

#include "interrupt_frame.h"
#include "../printf/printf.h"

void interrupt_handler(struct interrupt_frame *frame)
{
    kernel_printf("Interrupt vector: %d\n", frame->int_no);
}
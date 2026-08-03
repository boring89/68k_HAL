#ifndef KERNEL_INTERRUPT_IRQ_H
#define KERNEL_INTERRUPT_IRQ_H

#include <stdint.h>

void irq_handler(uint32_t irq);

void irq0(void);

void irq1(void);

#endif
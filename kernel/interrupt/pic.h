#ifndef KERNEL_INTERRUPT_PIC_H
#define KERNEL_INTERRUPT_PIC_H

void pic_init(void);

void pic_send_eoi(unsigned char irq);

#endif
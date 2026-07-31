#ifndef KERNEL_INTERRUPT_INTERRUPT_FRAME_H
#define KERNEL_INTERRUPT_INTERRUPT_FRAME_H

#include <stdint.h>

struct interrupt_frame
{
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;

    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;

    uint32_t int_no;
};

#endif
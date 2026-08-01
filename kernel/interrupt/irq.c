#include "irq.h"
#include "pic.h"

#include "../printf/printf.h"
#include "../timer/pit.h"

#include <stdint.h>

void irq_handler(uint32_t irq)
{
    switch(irq)
    {
        case 0:
            timer_tick();
            break;
        
        default:
            break;
    }

    pic_send_eoi(irq);
}
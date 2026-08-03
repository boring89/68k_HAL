#include "idt.h"
#include "isr.h"
#include "irq.h"

struct idt_entry
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtr;

static void idt_set_gate(uint8_t num, uint32_t handler)
{
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].selector = 0x08;
    idt[num].zero = 0;
    idt[num].type_attr = 0x8E;
    idt[num].offset_high = (handler >> 16) & 0xFFFF;
}

static void lidt(struct idt_ptr *ptr)
{
    asm volatile(
        "lidt (%0)"
        :
        : "r"(ptr));
}

void idt_init(void)
{
    for (int i = 0; i < 256; i++)
    {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (uint32_t)&idt;

    idt_set_gate(0x20, (uint32_t)irq0);
    idt_set_gate(0x21, (uint32_t)irq1);

    lidt(&idtr);
}
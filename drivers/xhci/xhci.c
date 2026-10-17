/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "xhci.h"

#include "hal/cpu.h"
#include "hal/dma.h"
#include "hal/irq.h"
#include "hal/timer.h"

#include "kernel/input/event_buffer.h"
#include "kernel/lib/k_memory.h"
#include "kernel/log/log.h"
#include "kernel/printf/printf.h"
#include "kernel/usb/descriptor.h"
#include "kernel/usb/hid.h"
#include "kernel/usb/usb.h"

/*
 * What this driver does, in the order it happens (see docs/usb.md):
 *
 *   1. take the controller from the BIOS, reset it, give it its data structures
 *      (device context array, command ring, event ring) and start it;
 *   2. for every port with something plugged in: reset the port, ask the controller for a
 *      device slot, give the device an address, read its descriptors;
 *   3. a HID boot keyboard or mouse gets SET_CONFIGURATION, SET_PROTOCOL(boot), and an
 *      interrupt endpoint that always has one transfer waiting; each finished transfer is a
 *      key or mouse report (kernel/usb/hid.c) and becomes input events.
 *
 * Everything the controller does is described by 16-byte records, TRBs (transfer request
 * blocks), in rings in memory: software writes TRBs into the command ring and the transfer rings
 * and rings a doorbell; the controller writes finished work into the event ring and interrupts.
 * The "cycle bit" of each TRB tells who owns it: the producer flips the meaning each time it goes
 * round the ring, so no head/tail registers are needed (xHCI 4.9).
 */

/* --- registers (xHCI 5.3 to 5.6) --- */

#define CAP_LENGTH 0x00 /* byte 0; HCIVERSION in the upper half */
#define CAP_HCSPARAMS1 0x04
#define CAP_HCSPARAMS2 0x08
#define CAP_HCCPARAMS1 0x10
#define CAP_DBOFF 0x14
#define CAP_RTSOFF 0x18

#define OP_USBCMD 0x00
#define OP_USBSTS 0x04
#define OP_CRCR 0x18
#define OP_DCBAAP 0x30
#define OP_CONFIG 0x38
#define OP_PORT(n) (0x400 + 0x10 * ((n) - 1)) /* n is 1-based */

#define CMD_RUN (1u << 0)
#define CMD_RESET (1u << 1)
#define CMD_INTERRUPTER_ENABLE (1u << 2)

#define STS_HALTED (1u << 0)
#define STS_EVENT_INTERRUPT (1u << 3)
#define STS_NOT_READY (1u << 11)

#define PORT_CONNECTED (1u << 0)
#define PORT_ENABLED (1u << 1)
#define PORT_RESET (1u << 4)
#define PORT_LINK_STATE(x) (((x) >> 5) & 0xF)
#define PORT_POWER (1u << 9)
#define PORT_SPEED(x) (((x) >> 10) & 0xF)
/* Change bits, cleared by writing 1 to them; all other writable bits are written back as read. */
#define PORT_CHANGE_BITS ((1u << 17) | (1u << 18) | (1u << 19) | (1u << 20) | (1u << 21) | (1u << 22) | (1u << 23))
#define PORT_RESET_CHANGE (1u << 21)
/* Bits that must be written as 0 unless meant: PED (writing 1 disables the port), PR, the link state write fields. */
#define PORT_PRESERVE (PORT_POWER | (3u << 14) | (1u << 25) | (1u << 26) | (1u << 27))

#define RT_IMAN(n) (0x20 + 0x20 * (n))
#define RT_IMOD(n) (0x24 + 0x20 * (n))
#define RT_ERSTSZ(n) (0x28 + 0x20 * (n))
#define RT_ERSTBA(n) (0x30 + 0x20 * (n))
#define RT_ERDP(n) (0x38 + 0x20 * (n))

#define IMAN_PENDING (1u << 0)
#define IMAN_ENABLE (1u << 1)
#define ERDP_BUSY (1u << 3)

/* --- TRBs (xHCI 6.4) --- */

typedef struct
{
    uint32_t parameter_low;
    uint32_t parameter_high;
    uint32_t status;
    uint32_t control;
} trb_t;

#define TRB_CYCLE (1u << 0)
#define TRB_TOGGLE_CYCLE (1u << 1)
#define TRB_INTERRUPT_ON_SHORT (1u << 2)
#define TRB_INTERRUPT_ON_COMPLETION (1u << 5)
#define TRB_IMMEDIATE_DATA (1u << 6)
#define TRB_DIRECTION_IN (1u << 16)
#define TRB_TYPE(x) ((uint32_t)(x) << 10)
#define TRB_TYPE_OF(control) (((control) >> 10) & 0x3F)

#define TRB_NORMAL 1
#define TRB_SETUP_STAGE 2
#define TRB_DATA_STAGE 3
#define TRB_STATUS_STAGE 4
#define TRB_LINK 6
#define TRB_ENABLE_SLOT 9
#define TRB_DISABLE_SLOT 10
#define TRB_ADDRESS_DEVICE 11
#define TRB_CONFIGURE_ENDPOINT 12
#define TRB_EVALUATE_CONTEXT 13
#define TRB_RESET_ENDPOINT 14
#define TRB_SET_DEQUEUE 16
#define TRB_TRANSFER_EVENT 32
#define TRB_COMMAND_COMPLETION 33
#define TRB_PORT_STATUS_CHANGE 34

#define COMPLETION_SUCCESS 1
#define COMPLETION_SHORT_PACKET 13

#define RING_TRBS 256 /* one 4 KiB page; the last is a link back to the start */

/* --- rings --- */

typedef struct
{
    trb_t *trbs;
    uint32_t phys;
    unsigned int enqueue;
    unsigned int cycle;
} ring_t;

static int ring_init(ring_t *ring)
{
    ring->trbs = hal_dma_alloc(RING_TRBS * sizeof(trb_t), &ring->phys);

    if (ring->trbs == 0)
        return -1;

    ring->enqueue = 0;
    ring->cycle = 1;

    /* The last TRB points back to the first and tells the controller to flip its cycle state. */
    trb_t *link = &ring->trbs[RING_TRBS - 1];

    link->parameter_low = ring->phys;
    link->control = TRB_TYPE(TRB_LINK) | TRB_TOGGLE_CYCLE;

    return 0;
}

static void ring_free(ring_t *ring)
{
    if (ring->trbs != 0)
        hal_dma_free(ring->trbs, RING_TRBS * sizeof(trb_t));

    ring->trbs = 0;
}

/* Put a TRB on the ring (the controller sees it once the cycle bit is right) and return its address. */
static uint32_t ring_push(ring_t *ring, uint32_t parameter_low, uint32_t parameter_high, uint32_t status,
                          uint32_t control)
{
    trb_t *trb = &ring->trbs[ring->enqueue];
    uint32_t address = ring->phys + ring->enqueue * (uint32_t)sizeof(trb_t);

    trb->parameter_low = parameter_low;
    trb->parameter_high = parameter_high;
    trb->status = status;

    /* The cycle bit goes in last: it is what hands the TRB over. */
    asm volatile("" : : : "memory");
    trb->control = control | (ring->cycle ? TRB_CYCLE : 0);

    ring->enqueue++;

    if (ring->enqueue == RING_TRBS - 1)
    {
        trb_t *link = &ring->trbs[RING_TRBS - 1];

        link->control = TRB_TYPE(TRB_LINK) | TRB_TOGGLE_CYCLE | (ring->cycle ? TRB_CYCLE : 0);
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }

    return address;
}

/* --- device state --- */

typedef struct
{
    ring_t ring;
    void *buffer;       /* where the controller writes a report */
    uint32_t buffer_phys;
    uint16_t length;    /* bytes per report */
    uint8_t dci;        /* device context index of the endpoint: 2 * number + 1 for IN */
    uint8_t protocol;   /* USB_HID_PROTOCOL_KEYBOARD or _MOUSE */
    uint8_t interface_number;
    uint8_t active;
    usb_hid_keyboard_t keyboard;
} hid_endpoint_t;

typedef struct
{
    int in_use;
    uint8_t slot;
    uint8_t port;
    uint8_t speed;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t device_class;
    int list_id; /* entry in the kernel's device list */

    /* Where it is in the tree: port is the port of its parent (the root hub when parent_slot is 0). */
    uint8_t parent_slot;
    uint8_t root_port;
    uint8_t depth;      /* hubs between it and the root hub */
    uint32_t route;     /* route string: 4 bits per hub on the way (xHCI 8.9) */
    uint8_t tt_slot;    /* the high speed hub whose transaction translator serves a low/full speed device */
    uint8_t tt_port;
    uint8_t is_hub;
    uint8_t hub_ports;
    uint8_t hub_multi_tt;
    uint8_t hub_think_time;

    void *output_context; /* the controller's view of the device */
    uint32_t output_phys;
    void *input_context;  /* what we ask it to change */
    uint32_t input_phys;
    ring_t control_ring;
    void *data;           /* control transfer data (one page) */
    uint32_t data_phys;

    volatile int control_done;
    volatile uint32_t control_code;

    hid_endpoint_t hid[2];
    unsigned int hid_count;
} device_t;

typedef struct
{
    int present;
    hal_pci_device_t pci;
    volatile uint8_t *capability;
    volatile uint8_t *operational;
    volatile uint8_t *runtime;
    volatile uint32_t *doorbells;

    unsigned int context_size; /* 32 or 64 bytes */
    unsigned int max_slots;
    unsigned int max_ports;

    uint32_t *dcbaa; /* 256 entries of 64 bits, as pairs of words */
    uint32_t dcbaa_phys;
    void *scratchpad_array;
    unsigned int scratchpad_count;
    void *scratchpad_pages[256];

    ring_t commands;
    volatile int command_done;
    volatile uint32_t command_code;
    volatile uint32_t command_slot;

    trb_t *events;
    uint32_t events_phys;
    unsigned int event_dequeue;
    unsigned int event_cycle;
    void *erst;

    device_t devices[XHCI_MAX_DEVICES + 1]; /* indexed by slot id (1..) */
    uint8_t port_slot[128];                 /* slot of the device on a port, 0 = none */
    volatile uint8_t port_dirty[128];       /* a port status change event arrived */
    volatile int any_port_dirty;
} xhci_t;

static xhci_t xhci;

/* --- register access --- */

static uint32_t read32(volatile uint8_t *base, unsigned int offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static void write32(volatile uint8_t *base, unsigned int offset, uint32_t value)
{
    *(volatile uint32_t *)(base + offset) = value;
}

static void write64(volatile uint8_t *base, unsigned int offset, uint32_t low, uint32_t high)
{
    write32(base, offset, low);
    write32(base, offset + 4, high);
}

static void delay_ms(unsigned int ms)
{
    uint64_t end = hal_timer_ns() + (uint64_t)ms * 1000000u;

    while (hal_timer_ns() < end)
        asm volatile("pause");
}

/* --- events --- */

static void hid_transfer_done(device_t *device, hid_endpoint_t *hid, uint32_t code);
static void queue_hid_transfer(device_t *device, hid_endpoint_t *hid);

/* Handle every event the controller has put in the event ring. Safe to call from the interrupt handler and from a polling loop (with interrupts off). */
static void process_events(void)
{
    xhci_t *x = &xhci;
    int handled = 0;

    for (;;)
    {
        trb_t *trb = &x->events[x->event_dequeue];

        if (((trb->control & TRB_CYCLE) != 0) != (x->event_cycle != 0))
            break;

        uint32_t type = TRB_TYPE_OF(trb->control);
        uint32_t code = trb->status >> 24;

        if (type == TRB_COMMAND_COMPLETION)
        {
            x->command_code = code;
            x->command_slot = trb->control >> 24;
            x->command_done = 1;
        }
        else if (type == TRB_TRANSFER_EVENT)
        {
            unsigned int slot = trb->control >> 24;
            unsigned int dci = (trb->control >> 16) & 0x1F;

            if (slot >= 1 && slot <= XHCI_MAX_DEVICES && x->devices[slot].in_use)
            {
                device_t *device = &x->devices[slot];

                if (dci == 1)
                {
                    device->control_code = code;
                    device->control_done = 1;
                }
                else
                {
                    for (unsigned int i = 0; i < device->hid_count; i++)
                    {
                        if (device->hid[i].dci == dci && device->hid[i].active)
                            hid_transfer_done(device, &device->hid[i], code);
                    }
                }
            }
        }
        else if (type == TRB_PORT_STATUS_CHANGE)
        {
            unsigned int port = trb->parameter_low >> 24;

            if (port >= 1 && port <= 127)
            {
                x->port_dirty[port] = 1;
                x->any_port_dirty = 1;
            }
        }

        x->event_dequeue++;

        if (x->event_dequeue == RING_TRBS)
        {
            x->event_dequeue = 0;
            x->event_cycle ^= 1;
        }

        handled = 1;
    }

    if (handled)
    {
        /* Tell the controller how far we got; writing 1 to the busy bit clears it. */
        uint32_t address = x->events_phys + x->event_dequeue * (uint32_t)sizeof(trb_t);

        write64(x->runtime, RT_ERDP(0), address | ERDP_BUSY, 0);
    }
}

static void interrupt_handler(void)
{
    xhci_t *x = &xhci;

    /* Acknowledge: the interrupter's pending bit, then the controller's event interrupt. */
    write32(x->runtime, RT_IMAN(0), IMAN_PENDING | IMAN_ENABLE);
    write32(x->operational, OP_USBSTS, STS_EVENT_INTERRUPT);
    process_events();
}

/* Run the event ring ourselves with interrupts off: the interrupt handler is the other user of it. */
static void poll_events(void)
{
    uint32_t state = hal_cpu_irq_save();

    process_events();
    hal_cpu_irq_restore(state);
}

/* --- commands and control transfers --- */

static int wait_until(volatile int *flag, unsigned int timeout_ms)
{
    uint64_t end = hal_timer_ns() + (uint64_t)timeout_ms * 1000000u;

    while (!*flag)
    {
        poll_events();

        if (hal_timer_ns() >= end)
            return -1;

        asm volatile("pause");
    }

    return 0;
}

/* Run one command and wait for it. Returns the completion code (1 = success), or 0 on timeout. */
static uint32_t run_command(uint32_t parameter_low, uint32_t control, uint32_t *slot)
{
    xhci_t *x = &xhci;

    x->command_done = 0;
    ring_push(&x->commands, parameter_low, 0, 0, control);
    asm volatile("" : : : "memory");
    x->doorbells[0] = 0;

    if (wait_until(&x->command_done, 1000) != 0)
        return 0;

    if (slot != 0)
        *slot = x->command_slot;

    return x->command_code;
}

/*
 * A control transfer on endpoint 0 (USB 2.0 spec 9.3): the 8-byte setup packet, then (if length
 * is not 0) the data in the direction the setup packet says, then a status stage in the other
 * direction. Three kinds of TRB. Data goes through device->data. Returns 0 or -1.
 */
static int control_transfer(device_t *device, const uint8_t setup[8], unsigned int length)
{
    xhci_t *x = &xhci;
    int data_in = (setup[0] & 0x80) != 0;
    uint32_t transfer_type = length == 0 ? 0 : (data_in ? 3 : 2); /* none, OUT data, IN data */

    device->control_done = 0;

    uint32_t low = (uint32_t)setup[0] | (uint32_t)setup[1] << 8 | (uint32_t)setup[2] << 16 | (uint32_t)setup[3] << 24;
    uint32_t high = (uint32_t)setup[4] | (uint32_t)setup[5] << 8 | (uint32_t)setup[6] << 16 | (uint32_t)setup[7] << 24;

    ring_push(&device->control_ring, low, high, 8,
              TRB_TYPE(TRB_SETUP_STAGE) | TRB_IMMEDIATE_DATA | (transfer_type << 16));

    if (length != 0)
    {
        /* No interrupt-on-short-packet here: a short answer would then produce an extra event before the status stage's. */
        ring_push(&device->control_ring, device->data_phys, 0, length,
                  TRB_TYPE(TRB_DATA_STAGE) | (data_in ? TRB_DIRECTION_IN : 0));
    }

    /* The status stage goes the other way (IN when there was no data or it was OUT). */
    ring_push(&device->control_ring, 0, 0, 0,
              TRB_TYPE(TRB_STATUS_STAGE) | TRB_INTERRUPT_ON_COMPLETION |
                  ((length == 0 || !data_in) ? TRB_DIRECTION_IN : 0));

    asm volatile("" : : : "memory");
    x->doorbells[device->slot] = 1; /* endpoint 0 = device context index 1 */

    if (wait_until(&device->control_done, 1000) != 0)
        return -1;

    return device->control_code == COMPLETION_SUCCESS || device->control_code == COMPLETION_SHORT_PACKET ? 0 : -1;
}

/* Any control request; an IN request's data (length bytes) arrives in device->data. */
static int control_request(device_t *device, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index,
                           unsigned int length)
{
    uint8_t setup[8] = {request_type, request, (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)index,
                        (uint8_t)(index >> 8), (uint8_t)length, (uint8_t)(length >> 8)};

    if (length != 0 && (request_type & 0x80))
        k_memset(device->data, 0, length);

    return control_transfer(device, setup, length);
}

static int get_descriptor(device_t *device, uint8_t type, uint8_t index, unsigned int length)
{
    return control_request(device, 0x80, 6, (uint16_t)(type << 8 | index), 0, length);
}

static int send_request(device_t *device, uint8_t request_type, uint8_t request, uint16_t value, uint16_t index)
{
    return control_request(device, request_type, request, value, index, 0);
}

/* --- contexts (xHCI 6.2) --- */

/* Context n (0 = slot, 1 = endpoint 0, ...) of the output device context; input contexts have the input control context first. */
static uint32_t *output_context(device_t *device, unsigned int index)
{
    return (uint32_t *)((uint8_t *)device->output_context + index * xhci.context_size);
}

static uint32_t *input_context(device_t *device, unsigned int index)
{
    return (uint32_t *)((uint8_t *)device->input_context + index * xhci.context_size);
}

static void clear_input(device_t *device)
{
    k_memset(device->input_context, 0, 33 * xhci.context_size);
}

/* Max packet size of the control endpoint before the device says (USB 2.0 9.6.1, USB 3.0 9.6.1). */
static unsigned int initial_control_packet_size(unsigned int speed)
{
    switch (speed)
    {
    case 4:
    case 5:
        return 512;

    case 3:
        return 64;

    default:
        return 8;
    }
}

static void free_device(device_t *device)
{
    for (unsigned int i = 0; i < 2; i++)
    {
        if (device->hid[i].ring.trbs != 0)
            ring_free(&device->hid[i].ring);

        if (device->hid[i].buffer != 0)
            hal_dma_free(device->hid[i].buffer, 4096);
    }

    ring_free(&device->control_ring);

    if (device->output_context != 0)
        hal_dma_free(device->output_context, 4096);

    if (device->input_context != 0)
        hal_dma_free(device->input_context, 4096);

    if (device->data != 0)
        hal_dma_free(device->data, 4096);

    k_memset(device, 0, sizeof(*device));
}

/* --- HID --- */

static void hid_transfer_done(device_t *device, hid_endpoint_t *hid, uint32_t code)
{
    if (code != COMPLETION_SUCCESS && code != COMPLETION_SHORT_PACKET)
    {
        /* Stalled or failed: stop polling this endpoint (recovery would need commands, which cannot run here). */
        hid->active = 0;

        return;
    }

    input_event_t events[USB_HID_MAX_EVENTS];

    if (hid->protocol == USB_HID_PROTOCOL_KEYBOARD)
    {
        int count = usb_hid_keyboard_report(&hid->keyboard, hid->buffer, hid->length, events);

        for (int i = 0; i < count; i++)
            input_event_push(&events[i]);
    }
    else
    {
        if (usb_hid_mouse_report(hid->buffer, hid->length, &events[0]) == 0)
            input_event_push(&events[0]);
    }

    queue_hid_transfer(device, hid);
}

static void queue_hid_transfer(device_t *device, hid_endpoint_t *hid)
{
    /* One Normal TRB: the controller polls the endpoint and fills the buffer when the device answers. */
    ring_push(&hid->ring, hid->buffer_phys, 0, hid->length,
              TRB_TYPE(TRB_NORMAL) | TRB_INTERRUPT_ON_SHORT | TRB_INTERRUPT_ON_COMPLETION);
    asm volatile("" : : : "memory");
    xhci.doorbells[device->slot] = hid->dci;
}

/* xHCI 6.2.3.6: the interval field is a power of two of 125 microsecond units. */
static unsigned int endpoint_interval(unsigned int speed, unsigned int interval)
{
    if (interval < 1)
        interval = 1;

    if (speed == 1 || speed == 2) /* full and low speed: bInterval is in milliseconds, 1-255 -> 3-10 */
    {
        if (interval > 255)
            interval = 255;

        unsigned int exponent = 0;

        while (interval > 1)
        {
            interval >>= 1;
            exponent++;
        }

        return exponent + 3;
    }

    /* High and super speed: bInterval is already 2^(n-1) in 125 us units, 1-16 -> 0-15. */
    return (interval > 16 ? 16 : interval) - 1;
}

static int configure_hid(device_t *device, const usb_hid_interface_t *found)
{
    hid_endpoint_t *hid = &device->hid[device->hid_count];
    unsigned int number = found->endpoint_address & 0xF;

    k_memset(hid, 0, sizeof(*hid));
    hid->dci = (uint8_t)(number * 2 + 1);
    hid->protocol = found->protocol;
    hid->interface_number = found->interface_number;
    hid->length = found->max_packet_size > 8 ? 8 : found->max_packet_size;

    hid->buffer = hal_dma_alloc(4096, &hid->buffer_phys);

    if (hid->buffer == 0 || ring_init(&hid->ring) != 0)
    {
        if (hid->buffer != 0)
            hal_dma_free(hid->buffer, 4096);

        k_memset(hid, 0, sizeof(*hid));

        return -1;
    }

    /* The boot protocol: fixed report layouts, no report descriptor needed (HID 1.11, 7.2.6). */
    if (send_request(device, 0x21, 0x0B, 0, found->interface_number) != 0)
        kernel_printf("xhci: slot %u: SET_PROTOCOL failed\n", device->slot);

    /* Idle rate 0: report only when something changes (7.2.4). Not every device implements it. */
    send_request(device, 0x21, 0x0A, 0, found->interface_number);

    /* Tell the controller about the new endpoint (Configure Endpoint command). */
    clear_input(device);
    k_memcpy(input_context(device, 1), output_context(device, 0), xhci.context_size); /* current slot context */

    uint32_t *slot = input_context(device, 1);
    uint32_t *endpoint = input_context(device, 1 + hid->dci);
    uint32_t *control = input_context(device, 0);

    if ((slot[0] >> 27) < hid->dci)
        slot[0] = (slot[0] & 0x07FFFFFFu) | ((uint32_t)hid->dci << 27);

    control[1] = 1u | (1u << hid->dci); /* add the slot context and the endpoint */

    unsigned int interval = endpoint_interval(device->speed, found->interval);

    endpoint[0] = interval << 16;
    /* Error count 3, endpoint type interrupt IN (7), max packet size (xHCI 6.2.3). */
    endpoint[1] = (3u << 1) | (7u << 3) | ((uint32_t)found->max_packet_size << 16);
    endpoint[2] = hid->ring.phys | 1u; /* dequeue pointer with the dequeue cycle state */
    endpoint[3] = 0;
    endpoint[4] = (uint32_t)hid->length | ((uint32_t)found->max_packet_size << 16);

    uint32_t code = run_command(device->input_phys, TRB_TYPE(TRB_CONFIGURE_ENDPOINT) | ((uint32_t)device->slot << 24), 0);

    if (code != COMPLETION_SUCCESS)
    {
        kernel_printf("xhci: slot %u: Configure Endpoint failed (%u)\n", device->slot, code);
        ring_free(&hid->ring);
        hal_dma_free(hid->buffer, 4096);
        k_memset(hid, 0, sizeof(*hid));

        return -1;
    }

    hid->active = 1;
    device->hid_count++;
    queue_hid_transfer(device, hid);

    return 0;
}

/* --- enumerating a device --- */

static int allocate_device(device_t *device)
{
    device->output_context = hal_dma_alloc(4096, &device->output_phys);
    device->input_context = hal_dma_alloc(4096, &device->input_phys);
    device->data = hal_dma_alloc(4096, &device->data_phys);

    if (device->output_context == 0 || device->input_context == 0 || device->data == 0 ||
        ring_init(&device->control_ring) != 0)
        return -1;

    return 0;
}

static const char *speed_name(unsigned int speed)
{
    return speed == 1 ? "full" : speed == 2 ? "low" : speed == 3 ? "high" : "super";
}

static void enumerate_children(device_t *hub);
static void remove_slot(unsigned int slot_id);

/*
 * Bring up the device that just appeared on `port` of `parent` (NULL: the root hub) at `speed`
 * (an xHCI port speed id).
 */
static void enumerate_device(unsigned int root_port, device_t *parent, unsigned int port, unsigned int speed)
{
    xhci_t *x = &xhci;

    if (speed == 0)
        return;

    uint32_t slot_id = 0;
    uint32_t code = run_command(0, TRB_TYPE(TRB_ENABLE_SLOT), &slot_id);

    if (code != COMPLETION_SUCCESS || slot_id < 1 || slot_id > XHCI_MAX_DEVICES)
    {
        kernel_printf("xhci: port %u: no device slot (%u)\n", port, code);

        return;
    }

    device_t *device = &x->devices[slot_id];

    k_memset(device, 0, sizeof(*device));
    device->slot = (uint8_t)slot_id;
    device->port = (uint8_t)port;
    device->root_port = (uint8_t)root_port;
    device->speed = (uint8_t)speed;
    device->list_id = -1;

    if (parent != NULL)
    {
        device->parent_slot = parent->slot;
        device->depth = (uint8_t)(parent->depth + 1);
        device->route = parent->route | ((uint32_t)(port > 15 ? 15 : port) << (4 * parent->depth));

        /* A low or full speed device behind a high speed hub talks through that hub's transaction translator. */
        if (parent->speed == 3 && (speed == 1 || speed == 2))
        {
            device->tt_slot = parent->slot;
            device->tt_port = (uint8_t)port;
        }
        else
        {
            device->tt_slot = parent->tt_slot;
            device->tt_port = parent->tt_port;
        }
    }

    if (allocate_device(device) != 0)
    {
        kernel_printf("xhci: port %u: out of memory\n", port);
        free_device(device);
        run_command(0, TRB_TYPE(TRB_DISABLE_SLOT) | (slot_id << 24), 0);

        return;
    }

    x->dcbaa[slot_id * 2] = device->output_phys;
    x->dcbaa[slot_id * 2 + 1] = 0;
    device->in_use = 1;

    if (parent == NULL)
        x->port_slot[port] = (uint8_t)slot_id;

    /* Address Device: the slot context (route, speed, root port) and endpoint 0 (control, ring, packet size). */
    clear_input(device);

    uint32_t *control = input_context(device, 0);
    uint32_t *slot = input_context(device, 1);
    uint32_t *endpoint = input_context(device, 2);
    unsigned int packet_size = initial_control_packet_size(speed);

    control[1] = 3; /* add the slot context and endpoint 0 */
    slot[0] = device->route | (speed << 20) | (1u << 27); /* route string; speed; one context entry (endpoint 0) */
    slot[1] = (uint32_t)root_port << 16;                  /* root hub port number */
    slot[2] = (uint32_t)device->tt_slot | ((uint32_t)device->tt_port << 8);
    endpoint[1] = (3u << 1) | (4u << 3) | ((uint32_t)packet_size << 16); /* error count 3, control, max packet */
    endpoint[2] = device->control_ring.phys | 1u;
    endpoint[4] = 8; /* average TRB length */

    code = run_command(device->input_phys, TRB_TYPE(TRB_ADDRESS_DEVICE) | (slot_id << 24), 0);

    if (code != COMPLETION_SUCCESS)
    {
        kernel_printf("xhci: port %u: Address Device failed (%u)\n", port, code);

        goto fail;
    }

    /* The first 8 bytes of the device descriptor give the real control packet size. */
    if (get_descriptor(device, USB_DT_DEVICE, 0, 8) != 0)
    {
        kernel_printf("xhci: port %u: no answer to GET_DESCRIPTOR\n", port);

        goto fail;
    }

    unsigned int real_size = ((uint8_t *)device->data)[7];

    if (speed >= 4)
        real_size = real_size < 16 ? 1u << real_size : 0;

    if ((speed == 1 || speed == 3) && real_size != 0 && real_size != packet_size)
    {
        /* Evaluate Context: change the packet size of endpoint 0. */
        clear_input(device);
        input_context(device, 0)[1] = 2;
        input_context(device, 2)[1] = (3u << 1) | (4u << 3) | ((uint32_t)real_size << 16);
        run_command(device->input_phys, TRB_TYPE(TRB_EVALUATE_CONTEXT) | (slot_id << 24), 0);
    }

    usb_device_descriptor_t description;

    if (get_descriptor(device, USB_DT_DEVICE, 0, 18) != 0 || usb_parse_device(device->data, 18, &description) != 0)
    {
        kernel_printf("xhci: port %u: cannot read the device descriptor\n", port);

        goto fail;
    }

    device->vendor_id = description.vendor_id;
    device->product_id = description.product_id;
    device->device_class = description.device_class;

    /* The configuration: first its length, then all of it. */
    if (get_descriptor(device, USB_DT_CONFIGURATION, 0, 9) != 0)
        goto fail;

    unsigned int total = usb_configuration_total_length(device->data, 9);

    if (total == 0 || total > 4096 || get_descriptor(device, USB_DT_CONFIGURATION, 0, total) != 0)
        goto fail;

    usb_hid_interface_t found[USB_MAX_HID_INTERFACES];
    int is_hub = 0;
    int count = usb_parse_configuration(device->data, total, found, USB_MAX_HID_INTERFACES, &is_hub);
    uint8_t configuration_value = ((uint8_t *)device->data)[5];

    /* A hub is a device of class 9 (in the device descriptor or in its interface). */
    is_hub = is_hub || description.device_class == USB_CLASS_HUB;

    const char *use = is_hub ? "hub" : count > 0 ? "HID" : "not used";

    kernel_printf("usb: %s port %u: %x:%x class %u, %s speed, %s\n", parent != NULL ? "hub" : "root", port,
                  description.vendor_id, description.product_id, description.device_class, speed_name(speed), use);

    if (is_hub)
    {
        if (device->depth >= 5)
        {
            kernel_printf("xhci: hubs nested too deep\n");
            use = "not used";
        }
        else if (send_request(device, 0x00, 9, configuration_value, 0) != 0)
        {
            goto fail;
        }
        else
        {
            /* The hub descriptor (USB 2.0 11.23.2.1; USB 3.0 10.13.2.1): number of ports, characteristics. */
            int super = speed >= 4;

            if (control_request(device, 0xA0, 6, (uint16_t)((super ? 0x2A : 0x29) << 8), 0, super ? 12 : 8) != 0)
            {
                kernel_printf("xhci: slot %u: no hub descriptor\n", slot_id);

                goto fail;
            }

            const uint8_t *hub_descriptor = device->data;

            device->hub_ports = hub_descriptor[2] > 15 ? 15 : hub_descriptor[2];
            device->hub_think_time = (hub_descriptor[3] >> 5) & 3;

            /* Tell the controller it is a hub (Configure Endpoint with the slot context only; xHCI 6.2.2). */
            clear_input(device);
            k_memcpy(input_context(device, 1), output_context(device, 0), x->context_size);
            input_context(device, 0)[1] = 1;

            uint32_t *hub_slot = input_context(device, 1);

            hub_slot[0] |= 1u << 26;                               /* hub */
            hub_slot[1] = (hub_slot[1] & 0x00FFFFFFu) | ((uint32_t)device->hub_ports << 24);
            hub_slot[2] = (hub_slot[2] & ~(3u << 16)) | ((uint32_t)device->hub_think_time << 16);

            if (run_command(device->input_phys, TRB_TYPE(TRB_CONFIGURE_ENDPOINT) | (slot_id << 24), 0) != COMPLETION_SUCCESS)
            {
                kernel_printf("xhci: slot %u: cannot make it a hub\n", slot_id);

                goto fail;
            }

            /* SuperSpeed hubs also want to know how deep they are (SET_HUB_DEPTH). */
            if (super)
                send_request(device, 0x20, 12, device->depth, 0);

            device->is_hub = 1;
            use = "hub";
        }
    }
    else if (count > 0)
    {
        if (send_request(device, 0x00, 9, configuration_value, 0) != 0)
        {
            kernel_printf("xhci: port %u: SET_CONFIGURATION failed\n", port);

            goto fail;
        }

        for (int i = 0; i < count && device->hid_count < 2; i++)
            configure_hid(device, &found[i]);

        if (device->hid_count > 0)
        {
            use = device->hid[0].protocol == USB_HID_PROTOCOL_KEYBOARD
                      ? (device->hid_count > 1 ? "keyboard+mouse" : "keyboard")
                      : "mouse";
        }
    }

    usb_device_info_t listed = {.port = (uint8_t)port, .speed = (uint8_t)speed, .vendor_id = description.vendor_id,
                                .product_id = description.product_id, .device_class = description.device_class};

    for (unsigned int i = 0; i < sizeof(listed.use) - 1 && use[i] != '\0'; i++)
        listed.use[i] = use[i];

    device->list_id = usb_device_add(&listed);

    if (device->is_hub)
        enumerate_children(device);

    return;

fail:
    if (parent == NULL)
        x->port_slot[port] = 0;

    x->dcbaa[slot_id * 2] = 0;
    run_command(0, TRB_TYPE(TRB_DISABLE_SLOT) | (slot_id << 24), 0);
    free_device(device);
}

/* --- hubs (USB 2.0 chapter 11, USB 3.0 chapter 10) --- */

#define HUB_PORT_POWER 8
#define HUB_PORT_RESET 4
#define HUB_C_PORT_CONNECTION 16
#define HUB_C_PORT_RESET 20

static int hub_port_status(device_t *hub, unsigned int port, uint32_t *status)
{
    if (control_request(hub, 0xA3, 0, 0, (uint16_t)port, 4) != 0)
        return -1;

    const uint8_t *data = hub->data;

    *status = (uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;

    return 0;
}

static device_t *child_on_port(const device_t *hub, unsigned int port)
{
    for (unsigned int slot = 1; slot <= XHCI_MAX_DEVICES; slot++)
    {
        device_t *d = &xhci.devices[slot];

        if (d->in_use && d->parent_slot == hub->slot && d->port == port)
            return d;
    }

    return NULL;
}

/* Look at one port of a hub: a device that arrived is reset and brought up; one that left is removed. */
static void scan_hub_port(device_t *hub, unsigned int port)
{
    uint32_t status;
    int super = hub->speed >= 4;

    if (hub_port_status(hub, port, &status) != 0)
        return;

    uint16_t port_status = (uint16_t)status;
    uint16_t port_change = (uint16_t)(status >> 16);

    if (port_change & 1)
        send_request(hub, 0x23, 1, HUB_C_PORT_CONNECTION, (uint16_t)port);

    device_t *child = child_on_port(hub, port);

    if (!(port_status & 1))
    {
        if (child != NULL)
            remove_slot(child->slot);

        return;
    }

    if (child != NULL)
        return;

    /* Reset the port and wait for the reset to finish (the hub sets the reset-change bit). */
    send_request(hub, 0x23, 3, HUB_PORT_RESET, (uint16_t)port);

    uint64_t end = hal_timer_ns() + 500000000u;

    for (;;)
    {
        delay_ms(10);

        if (hub_port_status(hub, port, &status) != 0)
            return;

        if (((status >> 16) & (1u << 4)) || hal_timer_ns() > end)
            break;
    }

    send_request(hub, 0x23, 1, HUB_C_PORT_RESET, (uint16_t)port);
    delay_ms(10);

    if (hub_port_status(hub, port, &status) != 0 || !(status & 2)) /* not enabled */
        return;

    /* The speed: USB 3 hub ports are always SuperSpeed; USB 2 hub ports say low (bit 9) or high (bit 10), else full. */
    unsigned int speed = super ? 4 : (status & (1u << 9)) ? 2 : (status & (1u << 10)) ? 3 : 1;

    enumerate_device(hub->root_port, hub, port, speed);
}

static void enumerate_children(device_t *hub)
{
    unsigned int hub_slot = hub->slot;

    /* Power the ports, then give the devices time to appear (and the power to settle). */
    for (unsigned int port = 1; port <= hub->hub_ports; port++)
        send_request(hub, 0x23, 3, HUB_PORT_POWER, (uint16_t)port);

    delay_ms(300);

    for (unsigned int port = 1; port <= hub->hub_ports; port++)
    {
        if (!xhci.devices[hub_slot].in_use)
            return;

        scan_hub_port(&xhci.devices[hub_slot], port);
    }
}

/* Hot plug behind hubs: look at every hub's ports now and then (every 250 ms). */
static void poll_hubs(void)
{
    static uint64_t next;
    uint64_t now = hal_timer_ns();

    if (now < next)
        return;

    next = now + 250000000u;

    for (unsigned int slot = 1; slot <= XHCI_MAX_DEVICES; slot++)
    {
        if (!xhci.devices[slot].in_use || !xhci.devices[slot].is_hub)
            continue;

        for (unsigned int port = 1; port <= xhci.devices[slot].hub_ports; port++)
        {
            if (!xhci.devices[slot].in_use)
                break;

            scan_hub_port(&xhci.devices[slot], port);
        }
    }
}

/* Take a device (and everything behind it, if it is a hub) away. */
static void remove_slot(unsigned int slot_id)
{
    xhci_t *x = &xhci;
    device_t *device = &x->devices[slot_id];

    if (!device->in_use)
        return;

    for (unsigned int slot = 1; slot <= XHCI_MAX_DEVICES; slot++)
    {
        if (x->devices[slot].in_use && x->devices[slot].parent_slot == slot_id)
            remove_slot(slot);
    }

    for (unsigned int i = 0; i < device->hid_count; i++)
        device->hid[i].active = 0;

    if (device->parent_slot == 0)
        x->port_slot[device->port] = 0;

    unsigned int port = device->port;

    usb_device_remove(device->list_id);
    run_command(0, TRB_TYPE(TRB_DISABLE_SLOT) | ((uint32_t)slot_id << 24), 0);
    x->dcbaa[slot_id * 2] = 0;
    free_device(device);
    kernel_printf("usb: port %u: device removed\n", port);
}

static void remove_device(unsigned int port)
{
    unsigned int slot_id = xhci.port_slot[port];

    if (slot_id != 0)
        remove_slot(slot_id);
}

/* --- ports --- */

static void write_port(unsigned int port, uint32_t bits)
{
    uint32_t current = read32(xhci.operational, OP_PORT(port));

    write32(xhci.operational, OP_PORT(port), (current & PORT_PRESERVE) | bits);
}

/* Reset a port that has a device (xHCI 4.19.5.1); returns 1 if it is enabled afterwards. */
static int reset_port(unsigned int port)
{
    uint32_t portsc = read32(xhci.operational, OP_PORT(port));

    /* A SuperSpeed device is trained by the hardware and shows up enabled in U0: no reset needed. */
    if ((portsc & PORT_ENABLED) && PORT_LINK_STATE(portsc) == 0)
    {
        write_port(port, portsc & PORT_CHANGE_BITS);

        return 1;
    }

    write_port(port, PORT_RESET | (portsc & PORT_CHANGE_BITS));

    uint64_t end = hal_timer_ns() + 500000000u;

    do
    {
        portsc = read32(xhci.operational, OP_PORT(port));

        if (portsc & PORT_RESET_CHANGE)
            break;

        delay_ms(1);
    } while (hal_timer_ns() < end);

    write_port(port, portsc & PORT_CHANGE_BITS);
    delay_ms(10); /* recovery time after reset (USB 2.0 7.1.7.5) */

    portsc = read32(xhci.operational, OP_PORT(port));

    return (portsc & PORT_ENABLED) != 0;
}

static void scan_port(unsigned int port)
{
    xhci_t *x = &xhci;
    uint32_t portsc = read32(x->operational, OP_PORT(port));

    /* Acknowledge the change bits we have seen. */
    if (portsc & PORT_CHANGE_BITS)
        write_port(port, portsc & PORT_CHANGE_BITS);

    if (!(portsc & PORT_CONNECTED))
    {
        remove_device(port);

        return;
    }

    if (x->port_slot[port] != 0)
        return;

    if (reset_port(port))
        enumerate_device(port, NULL, port, PORT_SPEED(read32(x->operational, OP_PORT(port))));
}

/* Called from the polling hook (never from an interrupt): look at ports whose status changed. */
static void service(void)
{
    xhci_t *x = &xhci;

    if (!x->present)
        return;

    /* The interrupt may not work (or not exist): run the event ring here as well. */
    poll_events();
    poll_hubs();

    if (!x->any_port_dirty)
        return;

    x->any_port_dirty = 0;

    for (unsigned int port = 1; port <= x->max_ports && port <= 127; port++)
    {
        if (x->port_dirty[port])
        {
            x->port_dirty[port] = 0;
            scan_port(port);
        }
    }
}

/* --- controller start-up --- */

static int wait_register(volatile uint8_t *base, unsigned int offset, uint32_t mask, uint32_t value,
                         unsigned int timeout_ms)
{
    uint64_t end = hal_timer_ns() + (uint64_t)timeout_ms * 1000000u;

    while ((read32(base, offset) & mask) != value)
    {
        if (hal_timer_ns() >= end)
            return -1;

        asm volatile("pause");
    }

    return 0;
}

/* xHCI 7.1: ownership is handed over through the USB Legacy Support capability. */
static void take_over_from_bios(xhci_t *x)
{
    unsigned int offset = (read32(x->capability, CAP_HCCPARAMS1) >> 16) * 4;

    for (int guard = 0; offset != 0 && guard < 32; guard++)
    {
        uint32_t header = read32(x->capability, offset);

        if ((header & 0xFF) == 1)
        {
            if (header & (1u << 16)) /* the BIOS owns it */
            {
                write32(x->capability, offset, header | (1u << 24)); /* ask for it */
                wait_register(x->capability, offset, 1u << 16, 0, 1000);
            }

            /* Turn off the BIOS's system management interrupts for this controller. */
            uint32_t control = read32(x->capability, offset + 4);

            control &= (3u << 1) | (0xFFu << 5) | (7u << 17);
            control |= 7u << 29;
            write32(x->capability, offset + 4, control);

            return;
        }

        unsigned int next = (header >> 8) & 0xFF;

        if (next == 0)
            return;

        offset += next * 4;
    }
}

static int start_controller(xhci_t *x)
{
    take_over_from_bios(x);

    /* Stop, then reset (xHCI 4.2). */
    write32(x->operational, OP_USBCMD, read32(x->operational, OP_USBCMD) & ~CMD_RUN);

    if (wait_register(x->operational, OP_USBSTS, STS_HALTED, STS_HALTED, 500) != 0)
        return -1;

    write32(x->operational, OP_USBCMD, CMD_RESET);

    if (wait_register(x->operational, OP_USBCMD, CMD_RESET, 0, 1000) != 0 ||
        wait_register(x->operational, OP_USBSTS, STS_NOT_READY, 0, 1000) != 0)
        return -1;

    uint32_t params1 = read32(x->capability, CAP_HCSPARAMS1);
    uint32_t params2 = read32(x->capability, CAP_HCSPARAMS2);
    uint32_t capability = read32(x->capability, CAP_HCCPARAMS1);

    x->max_slots = params1 & 0xFF;
    x->max_ports = params1 >> 24;
    x->context_size = (capability & (1u << 2)) ? 64 : 32;

    unsigned int slots = x->max_slots < XHCI_MAX_DEVICES ? x->max_slots : XHCI_MAX_DEVICES;

    write32(x->operational, OP_CONFIG, slots);

    /* Device context base address array (256 entries of 8 bytes; entry 0 is the scratchpad array). */
    x->dcbaa = hal_dma_alloc(4096, &x->dcbaa_phys);

    if (x->dcbaa == 0)
        return -1;

    x->scratchpad_count = (((params2 >> 21) & 0x1F) << 5) | ((params2 >> 27) & 0x1F);

    if (x->scratchpad_count > 0)
    {
        if (x->scratchpad_count > 256)
            return -1;

        uint32_t array_phys;

        x->scratchpad_array = hal_dma_alloc(4096, &array_phys);

        if (x->scratchpad_array == 0)
            return -1;

        for (unsigned int i = 0; i < x->scratchpad_count; i++)
        {
            uint32_t page_phys;

            x->scratchpad_pages[i] = hal_dma_alloc(4096, &page_phys);

            if (x->scratchpad_pages[i] == 0)
                return -1;

            ((uint64_t *)x->scratchpad_array)[i] = page_phys;
        }

        x->dcbaa[0] = array_phys;
        x->dcbaa[1] = 0;
    }

    write64(x->operational, OP_DCBAAP, x->dcbaa_phys, 0);

    /* Command ring (the Command Ring Control Register holds its address and the cycle state). */
    if (ring_init(&x->commands) != 0)
        return -1;

    write64(x->operational, OP_CRCR, x->commands.phys | 1u, 0);

    /* Event ring: one segment, described by a one-entry event ring segment table. */
    x->events = hal_dma_alloc(RING_TRBS * sizeof(trb_t), &x->events_phys);

    uint32_t erst_phys;

    x->erst = hal_dma_alloc(4096, &erst_phys);

    if (x->events == 0 || x->erst == 0)
        return -1;

    uint32_t *entry = x->erst;

    entry[0] = x->events_phys;
    entry[1] = 0;
    entry[2] = RING_TRBS;
    entry[3] = 0;

    x->event_dequeue = 0;
    x->event_cycle = 1;

    write32(x->runtime, RT_ERSTSZ(0), 1);
    write64(x->runtime, RT_ERDP(0), x->events_phys, 0);
    write64(x->runtime, RT_ERSTBA(0), erst_phys, 0);
    write32(x->runtime, RT_IMOD(0), 0);

    return 0;
}

static int xhci_probe(const hal_pci_device_t *pci)
{
    if (pci->prog_if != 0x30)
        return -1;

    if (pci->bar[0].type != HAL_PCI_BAR_MEM)
        return -1;

    volatile uint8_t *registers = hal_pci_map_bar(pci, 0);

    if (registers == 0)
        return -1;

    /* Always listed by `usb`, whether or not we can run it (the same record drivers/usb_host makes). */
    usb_host_info_t listed = {
        .type = USB_HOST_XHCI,
        .bus = pci->bus,
        .device = pci->device,
        .function = pci->function,
        .version = (uint16_t)(read32(registers, CAP_LENGTH) >> 16),
        .ports = (uint8_t)(read32(registers, CAP_HCSPARAMS1) >> 24),
        .slots = (uint8_t)(read32(registers, CAP_HCSPARAMS1) & 0xFF),
    };

    usb_host_register(&listed);

    if (xhci.present)
        return 0;

    xhci_t *x = &xhci;

    k_memset(x, 0, sizeof(*x));
    x->pci = *pci;
    x->capability = registers;

    hal_pci_enable(pci, HAL_PCI_COMMAND_MEMORY | HAL_PCI_COMMAND_BUS_MASTER);

    x->operational = x->capability + (read32(x->capability, CAP_LENGTH) & 0xFF);
    x->runtime = x->capability + read32(x->capability, CAP_RTSOFF);
    x->doorbells = (volatile uint32_t *)(x->capability + read32(x->capability, CAP_DBOFF));

    if (start_controller(x) != 0)
    {
        kernel_log(LOG_WARN, "xHCI: cannot start the controller");

        return 0;
    }

    /* Interrupts: a message (MSI) if the controller can, else its legacy line; the poll hook backs both up. */
    int line = hal_pci_enable_msi(pci, interrupt_handler);

    if (line < 0 && pci->irq_pin != 0 && pci->irq_line < 16 && hal_irq_register(pci->irq_line, interrupt_handler) == 0)
    {
        hal_irq_unmask(pci->irq_line);
        line = pci->irq_line;
    }

    write32(x->runtime, RT_IMAN(0), IMAN_PENDING | (line >= 0 ? IMAN_ENABLE : 0));
    write32(x->operational, OP_USBSTS, STS_EVENT_INTERRUPT);
    write32(x->operational, OP_USBCMD, CMD_RUN | (line >= 0 ? CMD_INTERRUPTER_ENABLE : 0));

    if (wait_register(x->operational, OP_USBSTS, STS_HALTED, 0, 500) != 0)
    {
        kernel_log(LOG_WARN, "xHCI: the controller did not start");

        return 0;
    }

    x->present = 1;
    usb_poller_register(service);

    /* Ports the controller does not power by itself (port power control) need switching on. */
    for (unsigned int port = 1; port <= x->max_ports && port <= 127; port++)
    {
        if (!(read32(x->operational, OP_PORT(port)) & PORT_POWER))
            write_port(port, PORT_POWER);
    }


    kernel_printf("xhci: %u ports, %u slots, %s interrupts\n", x->max_ports, x->max_slots,
                  line >= 16 ? "message" : line >= 0 ? "legacy" : "no");

    /* Devices need time to show up after the reset (USB 2.0 7.1.7.3: debounce), then look at every port. */
    delay_ms(200);

    for (unsigned int port = 1; port <= x->max_ports && port <= 127; port++)
    {
        uint32_t portsc = read32(x->operational, OP_PORT(port));

        if (portsc & PORT_CONNECTED)
            scan_port(port);
    }

    return 0;
}

const hal_pci_driver_t xhci_pci_driver = {
    .name = "xhci",
    .vendor_id = 0,
    .class_code = 0x0C,
    .subclass = 0x03,
    .probe = xhci_probe,
};

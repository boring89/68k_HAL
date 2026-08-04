/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "irq.h"
#include "pic.h"
#include "irq_callback.h"

#include <stdint.h>

void irq_handler(uint32_t irq)
{
    irq_dispatch((uint8_t)irq);

    pic_send_eoi((uint8_t)irq);
}
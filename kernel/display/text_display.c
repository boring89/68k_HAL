/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "text_display.h"

#include <stddef.h>

static const text_display_t *current;

int text_display_register(const text_display_t *display)
{
    if (display == NULL || current != NULL)
        return -1;

    if (display->put_cell == NULL || display->copy_cell == NULL ||
        display->set_cursor == NULL || display->clear == NULL)
        return -1;

    if (display->width <= 0 || display->height <= 0)
        return -1;

    current = display;

    return 0;
}

const text_display_t *text_display_get(void)
{
    return current;
}

void text_display_reset(void)
{
    current = NULL;
}

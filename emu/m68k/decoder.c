/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "decoder.h"

void m68k_decode(
    uint16_t opcode,
    m68k_instruction_t *instruction)
{
    instruction->opcode = opcode;

    instruction->group =
        (opcode >> 12) & 0x0F;

    instruction->register_field =
        (opcode >> 9) & 0x07;

    instruction->mode =
        (opcode >> 3) & 0x07;

    instruction->ea_register =
        opcode & 0x07;

    instruction->size = 0;
    instruction->direction = 0;

    instruction->type =
        M68K_INSTRUCTION_INVALID;

    /*
     * ADD immediate
     */
    if ((opcode & 0xFF00) == 0x0600)
    {
        instruction->type =
            M68K_INSTRUCTION_ADD;

        switch ((opcode >> 6) & 0x03)
        {
        case 0:
            instruction->size =
                M68K_SIZE_BYTE;
            break;

        case 1:
            instruction->size =
                M68K_SIZE_WORD;
            break;

        case 2:
            instruction->size =
                M68K_SIZE_LONG;
            break;

        default:
            instruction->type =
                M68K_INSTRUCTION_INVALID;
            break;
        }

        return;
    }

    /*
     * MOVE
     */
    if (instruction->group >= 0x1 &&
        instruction->group <= 0x3)
    {
        instruction->type =
            M68K_INSTRUCTION_MOVE;

        switch (instruction->group)
        {
        case 0x1:
            instruction->size =
                M68K_SIZE_BYTE;
            break;

        case 0x2:
            instruction->size =
                M68K_SIZE_LONG;
            break;

        case 0x3:
            instruction->size =
                M68K_SIZE_WORD;
            break;
        }

        return;
    }
}
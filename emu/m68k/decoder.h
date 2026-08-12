/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef M68K_DECODER_H
#define M68K_DECODER_H

#include <stdint.h>

#include "effective_address.h"

typedef enum
{
    M68K_INSTRUCTION_INVALID = 0,
    M68K_INSTRUCTION_MOVE,
    M68K_INSTRUCTION_ADD
} m68k_instruction_type_t;

typedef enum
{
    M68K_SIZE_BYTE = 1,
    M68K_SIZE_WORD = 2,
    M68K_SIZE_LONG = 4
} m68k_size_t;


typedef struct
{
    uint16_t opcode;

    m68k_instruction_type_t type;

    uint8_t group;

    uint8_t register_field;

    uint8_t mode;
    uint8_t ea_register;

    uint8_t size;
    uint8_t direction;

    m68k_ea_t ea;

} m68k_instruction_t;

void m68k_decode(
    uint16_t opcode,
    m68k_instruction_t *instruction);

#endif
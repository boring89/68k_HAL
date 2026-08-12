/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef M68K_EFFECTIVE_ADDRESS_H
#define M68K_EFFECTIVE_ADDRESS_H

#include <stdint.h>

typedef enum
{
    M68K_EA_DATA_REGISTER,
    M68K_EA_ADDRESS_REGISTER,
    M68K_EA_ADDRESS_INDIRECT,
    M68K_EA_POSTINCREMENT,
    M68K_EA_PREDECREMENT,
    M68K_EA_DISPLACEMENT,
    M68K_EA_INDEXED,
    M68K_EA_ABSOLUTE_WORD,
    M68K_EA_ABSOLUTE_LONG,
    M68K_EA_PC_DISPLACEMENT,
    M68K_EA_PC_INDEXED,
    M68K_EA_IMMEDIATE,
    M68K_EA_INVALID

} m68k_ea_type_t;

typedef struct
{
    m68k_ea_type_t type;

    uint8_t register_number;

} m68k_ea_t;

void m68k_ea_decode(
    uint8_t mode,
    uint8_t register_number,
    m68k_ea_t *ea);

#endif
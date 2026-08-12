/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "effective_address.h"

void m68k_ea_decode(
    uint8_t mode,
    uint8_t register_number,
    m68k_ea_t *ea)
{
    ea->register_number = register_number;

    switch (mode)
    {
        case 0:
            ea->type = M68K_EA_DATA_REGISTER;
            break;

        case 1:
            ea->type = M68K_EA_ADDRESS_REGISTER;
            break;

        case 2:
            ea->type = M68K_EA_ADDRESS_INDIRECT;
            break;

        case 3:
            ea->type = M68K_EA_POSTINCREMENT;
            break;

        case 4:
            ea->type = M68K_EA_PREDECREMENT;
            break;

        case 5:
            ea->type = M68K_EA_DISPLACEMENT;
            break;

        case 6:
            ea->type = M68K_EA_INDEXED;
            break;

        case 7:
            switch (register_number)
            {
                case 0:
                    ea->type = M68K_EA_ABSOLUTE_WORD;
                    break;

                case 1:
                    ea->type = M68K_EA_ABSOLUTE_LONG;
                    break;

                case 2:
                    ea->type = M68K_EA_PC_DISPLACEMENT;
                    break;

                case 3:
                    ea->type = M68K_EA_PC_INDEXED;
                    break;

                case 4:
                    ea->type = M68K_EA_IMMEDIATE;
                    break;

                default:
                    ea->type = M68K_EA_INVALID;
                    break;
            }

            break;

        default:
            ea->type = M68K_EA_INVALID;
            break;
    }
}
/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_X86_MMU_SPACE_H
#define ARCH_X86_MMU_SPACE_H

#include "paging_entry.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * Address spaces (hal_space_*, see docs/process.md). Each space has its own page directory.
 * A directory entry (4 MiB of addresses) is either private to the space - the entries of the
 * user address ranges - or shared: all spaces hold the same entry, so the kernel's page tables
 * exist once. A shared entry created later is copied into every directory.
 */

/* True if the directory entry dir_index covers addresses that belong to one space each. */
bool space_index_is_private(uint32_t dir_index);

/*
 * A shared directory entry was created in the current directory: store it in every space.
 * Called by paging_map before the new table is first used.
 */
void space_propagate_entry(uint32_t dir_index, paging_entry_t entry);

#endif

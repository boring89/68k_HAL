/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "paging.h"

#include "hal/boot.h"
#include "hal/mmu.h"

#include "kernel/lib/k_memory.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/pmm.h"
#include "kernel/panic/panic.h"
#include "arch/x86/cpu/cpu.h"

/* EFER (Intel SDM Vol.3A 2.2.1): bit 11 = NXE. */
#define MSR_EFER 0xC0000080u
#define EFER_NXE (1ull << 11)

#define PAGE_SIZE 4096ull
#define LARGE_PAGE_SIZE (2ull * 1024 * 1024)
#define ADDRESS_LIMIT_BOOT (4ull * 1024 * 1024 * 1024)

/* Linker symbols (linker64.ld): virtual addresses of the code and the read-only data. */
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];
extern uint8_t _kernel_ro_start[];
extern uint8_t _kernel_text_end[];
extern uint8_t _kernel_ro_end[];

static uint64_t kernel_pml4; /* physical address */
static int nx_enabled;

static uint64_t *table_at(uint64_t phys)
{
    return (uint64_t *)hal_phys_to_virt(phys);
}

static int is_canonical(uint64_t virt)
{
    uint64_t top = virt >> 47;

    return top == 0 || top == 0x1FFFF;
}

int paging_nx_enabled(void)
{
    return nx_enabled;
}

uint64_t paging_kernel_root(void)
{
    return kernel_pml4;
}

uint64_t paging_alloc_table(void)
{
    uint64_t frame = pmm_alloc_frame();

    if (frame == 0)
        return 0;

    k_memset(table_at(frame), 0, PAGE_SIZE);

    return frame;
}

/* paging_init: a table frame that the boot tables can still reach (below 4 GiB). */
static uint64_t alloc_boot_table(void)
{
    uint64_t frame = paging_alloc_table();

    if (frame == 0 || frame >= ADDRESS_LIMIT_BOOT)
        panic("paging: cannot allocate a page table below 4 GiB");

    return frame;
}

/* The direct map: the 2 MiB page at phys (aligned) in the kernel's tables. */
static void map_direct_page(uint64_t phys)
{
    uint64_t *pml4 = table_at(kernel_pml4);
    uint64_t *pdpt = table_at(paging64_entry_phys(pml4[PAGING_HHDM_SLOT]));
    unsigned int i3 = paging64_index(HAL_PHYS_BASE + phys, 3);

    if (!(pdpt[i3] & PAGING64_PRESENT))
        pdpt[i3] = paging64_entry_make(alloc_boot_table(), PAGING64_PRESENT | PAGING64_WRITABLE);

    uint64_t *pd = table_at(paging64_entry_phys(pdpt[i3]));
    unsigned int i2 = paging64_index(HAL_PHYS_BASE + phys, 2);

    if (!(pd[i2] & PAGING64_PRESENT))
    {
        uint64_t flags = PAGING64_PRESENT | PAGING64_WRITABLE | PAGING64_LARGE;

        if (nx_enabled)
            flags |= PAGING64_NO_EXECUTE;

        pd[i2] = paging64_entry_make(phys, flags);
    }
}

/* Map [start, end) of physical memory into the direct map, rounded outwards to 2 MiB pages. */
static void map_direct_range(uint64_t start, uint64_t end)
{
    /* One PML4 slot covers 512 GiB. */
    if (start >= 512ull * 1024 * 1024 * 1024)
        return;

    if (end > 512ull * 1024 * 1024 * 1024)
        end = 512ull * 1024 * 1024 * 1024;

    for (uint64_t page = start & ~(LARGE_PAGE_SIZE - 1); page < end; page += LARGE_PAGE_SIZE)
        map_direct_page(page);
}

void paging_init(void)
{
    nx_enabled = (cpu_rdmsr(MSR_EFER) & EFER_NXE) != 0;

    /*
     * The kernel image is addressed as physical + HAL_KERNEL_OFFSET; its sections are on whole
     * pages (linker64.ld).
     */
    uintptr_t ro_start = (uintptr_t)_kernel_ro_start;
    uintptr_t text_end = (uintptr_t)_kernel_text_end;
    uintptr_t ro_end = (uintptr_t)_kernel_ro_end;

    KASSERT(ro_start % PAGE_SIZE == 0 && text_end % PAGE_SIZE == 0 && ro_end % PAGE_SIZE == 0);

    kernel_pml4 = alloc_boot_table();

    uint64_t *pml4 = table_at(kernel_pml4);
    static const unsigned int slots[] = {PAGING_HHDM_SLOT, PAGING_WINDOW_SLOT, PAGING_KERNEL_SLOT};

    /* The upper half is fixed from now on: every address space copies these three entries. */
    for (unsigned int i = 0; i < sizeof(slots) / sizeof(slots[0]); i++)
        pml4[slots[i]] = paging64_entry_make(alloc_boot_table(), PAGING64_PRESENT | PAGING64_WRITABLE);

    /* The direct map covers the RAM the PMM can hand out and the boot data (multiboot tables, modules). */
    hal_memory_region_t region;

    for (unsigned int i = 0; hal_boot_memory_region(i, &region) == 0; i++)
    {
        if (region.type == HAL_MEMORY_AVAILABLE || region.type == HAL_MEMORY_ACPI_RECLAIMABLE)
            map_direct_range(region.base, region.base + region.length);
    }

    for (unsigned int i = 0; hal_boot_reserved_region(i, &region) == 0; i++)
    {
        if (region.length != 0)
            map_direct_range(region.base, region.base + region.length);
    }

    /* The first 2 MiB hold the boot code's data and the ACPI/BIOS areas the early code reads. */
    map_direct_range(0, LARGE_PAGE_SIZE);

    /* The kernel image: code executable and read-only, constants read-only, the rest writable and not executable. */
    uint64_t first = (uint64_t)(uintptr_t)_kernel_start & ~(PAGE_SIZE - 1);
    uint64_t last = ((uint64_t)(uintptr_t)_kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (uint64_t phys = first; phys < last; phys += PAGE_SIZE)
    {
        uint64_t virt = phys + HAL_KERNEL_OFFSET;
        uint64_t flags = PAGING64_PRESENT;

        if (virt >= ro_start && virt < text_end)
            ; /* code: read-only, executable */
        else if (virt >= text_end && virt < ro_end)
            flags |= nx_enabled ? PAGING64_NO_EXECUTE : 0;
        else
            flags |= PAGING64_WRITABLE | (nx_enabled ? PAGING64_NO_EXECUTE : 0);

        if (paging_map_in(kernel_pml4, (uintptr_t)virt, phys, flags) != 0)
            panic("paging: cannot map the kernel image");
    }

    /* Without WP, ring 0 writes ignore R/W = 0 (Intel SDM Vol.3A 2.5); boot64.asm sets it already. */
    cpu_write_cr3(kernel_pml4);
    cpu_write_cr0(cpu_read_cr0() | CPU_CR0_WP);
}

/*
 * The entry that maps virt in the address space with the PML4 at root: the leaf (a 4 KiB page, or a
 * 2 MiB / 1 GiB page), its level in *level (1, 2, 3). NULL if virt is not mapped.
 */
static uint64_t *find_entry_in(uint64_t root, uintptr_t virt, unsigned int *level)
{
    if (!is_canonical(virt))
        return 0;

    uint64_t *table = table_at(root);

    for (unsigned int l = 4; l >= 1; l--)
    {
        uint64_t *entry = &table[paging64_index(virt, l)];

        if (!(*entry & PAGING64_PRESENT))
            return 0;

        if (l == 1 || (l <= 3 && (*entry & PAGING64_LARGE)))
        {
            *level = l;

            return entry;
        }

        table = table_at(paging64_entry_phys(*entry));
    }

    return 0;
}

static uint64_t *find_entry(uintptr_t virt, unsigned int *level)
{
    return find_entry_in(cpu_read_cr3() & PAGING64_ADDRESS_MASK, virt, level);
}

int paging_get_phys(uintptr_t virt, uintptr_t *phys)
{
    unsigned int level;
    uint64_t *entry;

    if (phys == 0 || (entry = find_entry(virt, &level)) == 0)
        return -1;

    uint64_t page_size = PAGE_SIZE << (9 * (level - 1));

    *phys = (uintptr_t)((*entry & PAGING64_ADDRESS_MASK & ~(page_size - 1)) | (virt & (page_size - 1)));

    return 0;
}

int paging_map_in(uint64_t root, uintptr_t virt, uint64_t phys, uint64_t flags)
{
    if (virt % PAGE_SIZE != 0 || phys % PAGE_SIZE != 0 || !is_canonical(virt))
        return -1;

    int kernel_half = paging64_is_kernel_address(virt);
    /* Restrictions go in the PTEs; the upper levels stay permissive (the CPU needs all levels to allow an access, SDM Vol.3A 4.6.1). */
    uint64_t table_flags = PAGING64_PRESENT | PAGING64_WRITABLE | (kernel_half ? 0 : PAGING64_USER);
    uint64_t *table = table_at(root);

    for (unsigned int level = 4; level > 1; level--)
    {
        uint64_t *entry = &table[paging64_index(virt, level)];

        if (!(*entry & PAGING64_PRESENT))
        {
            /* The upper-half PML4 entries are made by paging_init and copied into every space. */
            if (level == 4 && kernel_half)
                return -1;

            uint64_t frame = paging_alloc_table();

            if (frame == 0)
                return -1;

            *entry = paging64_entry_make(frame, table_flags);
        }
        else if (level <= 3 && (*entry & PAGING64_LARGE))
        {
            return -1;
        }

        table = table_at(paging64_entry_phys(*entry));
    }

    uint64_t *entry = &table[paging64_index(virt, 1)];

    /* Remapping silently is usually a bug: callers must unmap first. */
    if (*entry & PAGING64_PRESENT)
        return -1;

    *entry = paging64_entry_make(phys, flags | PAGING64_PRESENT);

    return 0;
}

int paging_map(uintptr_t virt, uint64_t phys, uint64_t flags)
{
    if (paging_map_in(cpu_read_cr3() & PAGING64_ADDRESS_MASK, virt, phys, flags) != 0)
        return -1;

    /* A not-present entry is not cached, but be safe about stale paging-structure caches. */
    cpu_invlpg(virt);

    return 0;
}

int paging_unmap(uintptr_t virt)
{
    unsigned int level;
    uint64_t *entry;

    if (virt % PAGE_SIZE != 0 || (entry = find_entry(virt, &level)) == 0 || level != 1)
        return -1;

    *entry = 0;

    /* Without this the CPU may keep using the old translation. */
    cpu_invlpg(virt);

    return 0;
}

int paging_protect(uintptr_t virt, uint64_t flags)
{
    unsigned int level;
    uint64_t *entry;

    if (virt % PAGE_SIZE != 0 || (entry = find_entry(virt, &level)) == 0 || level != 1)
        return -1;

    *entry = paging64_entry_make(paging64_entry_phys(*entry), flags | PAGING64_PRESENT);

    /* Stale translations would keep the old permissions. */
    cpu_invlpg(virt);

    return 0;
}

int paging_get_flags(uintptr_t virt, uint64_t *flags)
{
    unsigned int level;
    uint64_t *entry = find_entry(virt & ~(uintptr_t)(PAGE_SIZE - 1), &level);

    if (entry == 0 || flags == 0)
        return -1;

    *flags = *entry & ~PAGING64_ADDRESS_MASK;

    return 0;
}

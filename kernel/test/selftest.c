/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Only built into the kernel by `make test-qemu` (-DKERNEL_SELFTEST). */
#ifdef KERNEL_SELFTEST

#include "selftest.h"

#include "hal/boot.h"
#include "hal/clock.h"
#include "hal/mmu.h"

#include "kernel/block/block.h"
#include "kernel/block/ramdisk.h"
#include "kernel/fs/volume.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/m68k/guest.h"
#include "kernel/printf/printf.h"
#include "kernel/user/console.h"
#include "kernel/user/process.h"
#include "kernel/vfs/vfs.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"

#include "emu/m68k/trap.h"
#include "emu/m68k/vm.h"
#include "hal/cpu.h"
#include "hal/timer.h"

#include <stdint.h>

static int selftest_failures;

void selftest_report_failure(const char *file, int line, const char *cond)
{
    selftest_failures++;
    kernel_printf("  %s:%d: assertion failed: %s\n", file, line, cond);
}

int selftest_failure_count(void)
{
    return selftest_failures;
}

static void test_heap_alloc_free(void)
{
    SELFTEST_ASSERT(kmalloc(0) == 0);

    void *a = kmalloc(16);
    SELFTEST_ASSERT(a != 0);

    void *b = kmalloc(32);
    SELFTEST_ASSERT(b != 0);
    SELFTEST_ASSERT(a != b);

    kfree(a);

    /* A freed block of the same size should be reused. */
    void *c = kmalloc(16);
    SELFTEST_ASSERT(c == a);

    kfree(b);
    kfree(c);
}

/* The heap is at its own virtual range, backed by PMM frames marked used. */
static void test_heap_region_reserved_in_pmm(void)
{
    uintptr_t start = heap_get_start();

    SELFTEST_ASSERT(start == HEAP_VIRT_BASE);

    int unmarked = 0;

    for (uintptr_t offset = 0; offset < KERNEL_HEAP_SIZE;
         offset += PMM_FRAME_SIZE)
    {
        uintptr_t phys = 0;

        if (hal_mmu_get_phys(start + offset, &phys) != 0 ||
            !pmm_is_frame_used(phys))
            unmarked++;
    }

    SELFTEST_ASSERT(unmarked == 0);
}

/* Allocating past the first 1 MiB maps more frames at the end of the heap. */
static void test_heap_grows(void)
{
    size_t free_before = pmm_get_free_frames();
    size_t big = 3u * 1024 * 1024;

    uint8_t *block = kmalloc(big);

    SELFTEST_ASSERT(block != 0);

    if (block == 0)
        return;

    SELFTEST_ASSERT((uintptr_t)block >= HEAP_VIRT_BASE);
    SELFTEST_ASSERT((uintptr_t)block + big <= HEAP_VIRT_LIMIT);
    SELFTEST_ASSERT(free_before - pmm_get_free_frames() >=
                    (big - KERNEL_HEAP_SIZE) / PMM_FRAME_SIZE);

    /* The whole block is writable and keeps what was written. */
    for (size_t i = 0; i < big; i += 4093)
        block[i] = (uint8_t)(i / 4093);

    int bad = 0;

    for (size_t i = 0; i < big; i += 4093)
        bad += block[i] != (uint8_t)(i / 4093);

    SELFTEST_ASSERT(bad == 0);
    SELFTEST_ASSERT(kfree(block) == 0);

    /* The memory stays mapped and is reused without growing again. */
    size_t free_after = pmm_get_free_frames();
    void *again = kmalloc(big);

    SELFTEST_ASSERT(again == block);
    SELFTEST_ASSERT(pmm_get_free_frames() == free_after);
    SELFTEST_ASSERT(kfree(again) == 0);
}


static void test_hal_timer_ticks(void)
{
    uint32_t start = hal_timer_ticks();

    /* Each idle wakes on the next interrupt; PIT runs at 100 Hz. */
    for (int i = 0; i < 1000 && hal_timer_ticks() == start; i++)
        hal_cpu_idle();

    SELFTEST_ASSERT(hal_timer_ticks() != start);
}

/* No keyboard input during the run, so the wake-up is the PIT tick. */
static void test_hal_cpu_idle(void)
{
    uint32_t start = hal_timer_ticks();

    hal_cpu_idle();

    SELFTEST_ASSERT(hal_timer_ticks() != start);
}

/* Runs a 68k program in the guest RAM: arithmetic plus an A-line trap. */
static void test_m68k_guest_program(void)
{
    static const uint8_t program[] = {
        0x70, 0x15,                 /* MOVEQ #21,D0 */
        0xD0, 0x80,                 /* ADD.L D0,D0 */
        0xA9, 0xF4,                 /* A-line trap $A9F4 */
        0x72, 0x07,                 /* MOVEQ #7,D1 */
        0x4E, 0x72, 0x27, 0x00,     /* STOP #$2700 */
    };
    m68k_trap_record_t record;

    SELFTEST_ASSERT(m68k_guest_init() == 0);
    SELFTEST_ASSERT(m68k_vm_load(0x1000, program, sizeof(program)) == 0);
    SELFTEST_ASSERT(m68k_vm_reset(0x1000, 0x8000) == 0);

    m68k_vm_run(10000);

    SELFTEST_ASSERT(m68k_vm_halted());
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0) == 42);
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0 + 1) == 7);
    SELFTEST_ASSERT(m68k_trap_total() == 1);
    SELFTEST_ASSERT(m68k_trap_log_get(0, &record) == 0);
    SELFTEST_ASSERT(record.opcode == 0xA9F4);
    SELFTEST_ASSERT(record.pc == 0x1004);
}

/* DIVU.L with a 64-bit dividend: Musashi calls our 64-bit division helpers. */
static void test_m68k_guest_divide_64(void)
{
    static const uint8_t program[] = {
        0x70, 0x05,                 /* MOVEQ #5,D0 */
        0x74, 0x01,                 /* MOVEQ #1,D2 */
        0x72, 0x07,                 /* MOVEQ #7,D1 */
        0x4C, 0x41, 0x04, 0x02,     /* DIVU.L D1,D2:D0 */
        0x4E, 0x72, 0x27, 0x00,     /* STOP #$2700 */
    };

    SELFTEST_ASSERT(m68k_guest_init() == 0);
    SELFTEST_ASSERT(m68k_vm_load(0x1000, program, sizeof(program)) == 0);
    SELFTEST_ASSERT(m68k_vm_reset(0x1000, 0x8000) == 0);

    m68k_vm_run(10000);

    /* (1 << 32 | 5) / 7 = 613566757, remainder 2 */
    SELFTEST_ASSERT(m68k_vm_halted());
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0) == 613566757u);
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0 + 2) == 2);
}

/* The boot module became the block device ram0 (kernel_main registered it). */
static void test_block_ramdisk_module(void)
{
    block_device_t *disk = block_device_find("ram0");

    SELFTEST_ASSERT(disk != 0);

    if (disk == 0)
        return;

    SELFTEST_ASSERT(disk->block_size == 512);
    SELFTEST_ASSERT(disk->block_count == 2);

    uint8_t blocks[1024];

    SELFTEST_ASSERT(block_read(disk, 0, 2, blocks) == 0);
    SELFTEST_ASSERT(k_memcmp(blocks, "selftest module\n", 16) == 0);
    SELFTEST_ASSERT(blocks[16] == 0 && blocks[1023] == 0);

    SELFTEST_ASSERT(block_read(disk, 1, 1, blocks) == 0);
    SELFTEST_ASSERT(block_read(disk, 2, 1, blocks) == -1);
}

/* make test-qemu attaches a 300-sector IDE disk: byte i of sector s is s * 13 + i. */
static void test_ata_disk(void)
{
    block_device_t *disk = block_device_find("ata0");

    SELFTEST_ASSERT(disk != 0);

    if (disk == 0)
        return;

    SELFTEST_ASSERT(disk->block_size == 512);
    SELFTEST_ASSERT(disk->block_count == 300);

    uint8_t *sectors = kmalloc(300 * 512);

    SELFTEST_ASSERT(sectors != 0);

    if (sectors == 0)
        return;

    /* All 300 at once: the driver needs two commands for that. */
    SELFTEST_ASSERT(block_read(disk, 0, 300, sectors) == 0);

    int bad = 0;

    for (unsigned int s = 0; s < 300; s++)
    {
        for (unsigned int i = 0; i < 512; i++)
            bad += sectors[s * 512 + i] != (uint8_t)(s * 13 + i);
    }

    SELFTEST_ASSERT(bad == 0);

    /* A single sector from the middle, and the last one. */
    uint8_t one[512];

    SELFTEST_ASSERT(block_read(disk, 256, 1, one) == 0);
    SELFTEST_ASSERT(one[0] == (uint8_t)(256 * 13) && one[511] == (uint8_t)(256 * 13 + 511));
    SELFTEST_ASSERT(block_read(disk, 299, 1, one) == 0);
    SELFTEST_ASSERT(one[5] == (uint8_t)(299 * 13 + 5));
    SELFTEST_ASSERT(block_read(disk, 300, 1, one) == -1);

    kfree(sectors);
}

/* The second boot module is a disk with an HFS volume (tools/make_disk_image.py). */
static void test_hfs_volume_mounted(void)
{
    hfs_volume_t *volume = volume_get();

    SELFTEST_ASSERT(volume != 0);

    if (volume == 0)
        return;

    hfs_entry_t entry;
    char text[32];

    SELFTEST_ASSERT(k_strcmp(volume->name, "TESTVOL") == 0);
    SELFTEST_ASSERT(hfs_lookup(volume, "docs:deep:deeper:leaf", &entry) == 0);
    SELFTEST_ASSERT(hfs_lookup(volume, "ReadMe", &entry) == 0);
    SELFTEST_ASSERT(hfs_read(volume, &entry, HFS_FORK_DATA, 0, text,
                             sizeof(text)) == 15);
    SELFTEST_ASSERT(k_memcmp(text, "Hello from HFS\r", 15) == 0);
    SELFTEST_ASSERT(hfs_lookup(volume, "Nothing", &entry) == -1);
}

/* The guest RAM is 8 MiB by default and can be resized. */
static void test_m68k_guest_ram_size(void)
{
    SELFTEST_ASSERT(m68k_guest_ram_size() == 8u * 1024 * 1024);
    SELFTEST_ASSERT(m68k_guest_init() == 0);

    /* The last longword of the 8 MiB is reachable, the next one is not. */
    m68k_vm_write32(8u * 1024 * 1024 - 4, 0xCAFEF00Du);
    SELFTEST_ASSERT(m68k_vm_read32(8u * 1024 * 1024 - 4) == 0xCAFEF00Du);
    SELFTEST_ASSERT(*(volatile uint32_t *)(M68K_GUEST_RAM_BASE +
                                           8u * 1024 * 1024 - 4) ==
                    0x0DF0FECAu);
    m68k_vm_write32(8u * 1024 * 1024, 0x12345678u);
    SELFTEST_ASSERT(m68k_vm_read32(8u * 1024 * 1024) == 0);

    /* Resizing frees the old frames and allocates new ones on init. */
    size_t free_before = pmm_get_free_frames();

    SELFTEST_ASSERT(m68k_guest_set_size(1u) == -1);
    SELFTEST_ASSERT(m68k_guest_set_size(0) == -1);
    SELFTEST_ASSERT(m68k_guest_set_size(M68K_GUEST_RAM_MAX_SIZE + 4096) == -1);
    SELFTEST_ASSERT(m68k_guest_ram_size() == 8u * 1024 * 1024);

    SELFTEST_ASSERT(m68k_guest_set_size(16u * 1024 * 1024) == 0);
    SELFTEST_ASSERT(pmm_get_free_frames() == free_before + 2048);
    SELFTEST_ASSERT(m68k_guest_init() == 0);
    /* A bigger mapping also needs page tables, which are never freed. */
    SELFTEST_ASSERT(pmm_get_free_frames() <= free_before - 2048);
    SELFTEST_ASSERT(pmm_get_free_frames() >= free_before - 2048 - 4);
    m68k_vm_write32(16u * 1024 * 1024 - 4, 0xCAFEF00Du);
    SELFTEST_ASSERT(m68k_vm_read32(16u * 1024 * 1024 - 4) == 0xCAFEF00Du);

    SELFTEST_ASSERT(m68k_guest_set_size(M68K_GUEST_RAM_DEFAULT_SIZE) == 0);
    SELFTEST_ASSERT(m68k_guest_init() == 0);
    SELFTEST_ASSERT(pmm_get_free_frames() <= free_before);
    SELFTEST_ASSERT(pmm_get_free_frames() >= free_before - 4);
}

/* The guest has no FPU: an F-line opcode must reach its vector 11 handler. */
static void test_m68k_guest_fline_exception(void)
{
    static const uint8_t program[] = {
        0x21, 0xFC, 0x00, 0x00, 0x20, 0x00, 0x00, 0x2C, /* MOVE.L #$2000,$2C */
        0xF2, 0x80, 0x00, 0x00,                         /* FNOP */
        0x4E, 0x72, 0x27, 0x00,                         /* STOP #$2700 */
    };
    static const uint8_t handler[] = {
        0x7C, 0x09,                 /* MOVEQ #9,D6 */
        0x4E, 0x72, 0x27, 0x00,     /* STOP #$2700 */
    };

    SELFTEST_ASSERT(m68k_guest_init() == 0);
    SELFTEST_ASSERT(m68k_vm_load(0x1000, program, sizeof(program)) == 0);
    SELFTEST_ASSERT(m68k_vm_load(0x2000, handler, sizeof(handler)) == 0);
    SELFTEST_ASSERT(m68k_vm_reset(0x1000, 0x8000) == 0);

    m68k_vm_run(10000);

    SELFTEST_ASSERT(m68k_vm_halted());
    SELFTEST_ASSERT(m68k_vm_get_reg(M68K_VM_REG_D0 + 6) == 9);
}

/* QEMU's RTC starts at the host's time, so the year is at least 2020. */
static void test_hal_clock(void)
{
    hal_datetime_t now;

    SELFTEST_ASSERT(hal_clock_now(NULL) == -1);
    SELFTEST_ASSERT(hal_clock_now(&now) == 0);
    SELFTEST_ASSERT(now.year >= 2020 && now.year < 2100);
    SELFTEST_ASSERT(now.month >= 1 && now.month <= 12);
    SELFTEST_ASSERT(now.day >= 1 && now.day <= 31);
    SELFTEST_ASSERT(now.hour < 24 && now.minute < 60 && now.second < 60);
}

/* make test-qemu passes -initrd "<file> testmod"; the file is 1 KiB, the first bytes are the text. */
static void test_boot_module(void)
{
    static const char expected[] = "selftest module\n";
    const size_t module_size = 1024;
    hal_boot_module_t module;
    unsigned int found = 0;

    SELFTEST_ASSERT(hal_boot_module_count() >= 1);
    SELFTEST_ASSERT(hal_boot_module(hal_boot_module_count(), &module) == -1);
    SELFTEST_ASSERT(hal_boot_module(0, NULL) == -1);

    for (unsigned int i = 0; hal_boot_module(i, &module) == 0; i++)
    {
        const char *name = module.name;
        unsigned int length = 0;

        while (name[length] != '\0')
            length++;

        /* QEMU gives "<file> <args>"; compare the last word. */
        if (length < 7 || k_strcmp(name + length - 7, "testmod") != 0)
            continue;

        found++;
        SELFTEST_ASSERT(module.length == module_size);
        SELFTEST_ASSERT(k_memcmp((const void *)(uintptr_t)module.base,
                                 expected, sizeof(expected) - 1) == 0);

        /* Read-only mapping, and its frames must not be handed out. */
        uintptr_t phys = 0;

        SELFTEST_ASSERT(hal_mmu_get_phys((uintptr_t)module.base, &phys) == 0);
        SELFTEST_ASSERT(pmm_is_frame_used(module.base & ~0xFFFu) == 1);
    }

    SELFTEST_ASSERT(found == 1);
}

/*
 * User programs from the initrd (make initrd; skipped if the test run
 * has none). Each one runs and its console output must equal
 * /expected/<name>.txt; the same programs give the same output when
 * run natively on a Linux host (see docs/userland.md).
 */
static const struct
{
    const char *name;
    int status;            /* expected exit status */
    const char *argument1; /* arguments after the program's name, NULL = none */
    const char *argument2;
} user_tests[] = {
    {"hello", 0, NULL, NULL},
    {"printf", 0, NULL, NULL},
    {"malloc", 0, NULL, NULL},
    {"files", 0, NULL, NULL},
    {"sort", 0, NULL, NULL},
    {"args", 3, "one", "two"},
    {"time", 0, NULL, NULL},
    {"hfs", 0, NULL, NULL},
    {"lua", 0, "/data/test.lua", NULL}, /* the Lua interpreter, a ported program */
    {"crash", 139, NULL, NULL},         /* 128 + SIGSEGV */
};

static void test_user_programs(void)
{
    vfs_stat_t stat;

    if (vfs_stat("/bin/hello", &stat) != 0)
    {
        kernel_printf("  (no initrd: user programs skipped, stat %d)\n", (int)vfs_stat("/bin/hello", &stat));
        return;
    }

    static char output[4096];
    static char expected[4096];

    for (unsigned int i = 0; i < sizeof(user_tests) / sizeof(user_tests[0]); i++)
    {
        char path[64];
        const char *name = user_tests[i].name;

        k_strcpy(path, "/bin/");
        k_strcpy(path + 5, name);

        const char *args[] = {path, user_tests[i].argument1, user_tests[i].argument2, NULL};
        static const char *const environment[] = {"PATH=/bin", NULL};

        user_console_capture_start(output, sizeof(output));

        int32_t status = user_run(path, args, environment);

        user_console_capture_stop();

        char expected_path[64];

        k_strcpy(expected_path, "/expected/");
        k_strcpy(expected_path + 10, name);
        k_strcpy(expected_path + 10 + k_strlen(name), ".txt");

        vfs_file_t *file;
        int32_t length = 0;

        if (vfs_open(expected_path, VFS_O_RDONLY, 0, &file) == 0)
        {
            length = vfs_read(file, expected, sizeof(expected) - 1);
            vfs_file_close(file);
        }

        expected[length > 0 ? length : 0] = '\0';

        if (status != user_tests[i].status || k_strcmp(output, expected) != 0)
        {
            kernel_printf("  %s: exit %d (want %d)\n--- got\n%s--- want\n%s---\n", name,
                          (int)status, user_tests[i].status, output, expected);
        }

        SELFTEST_ASSERT(status == user_tests[i].status);
        SELFTEST_ASSERT(k_strcmp(output, expected) == 0);
    }
}

void selftest_abort(void)
{
    selftest_platform_exit(0);
}

void selftest_run(void)
{
    kernel_printf("[TEST] selftest start\n");

    selftest_failures = 0;

    SELFTEST_RUN(test_heap_alloc_free);
    SELFTEST_RUN(test_heap_region_reserved_in_pmm);
    SELFTEST_RUN(test_heap_grows);
    SELFTEST_RUN(test_hal_timer_ticks);
    SELFTEST_RUN(test_hal_cpu_idle);
    SELFTEST_RUN(test_boot_module);
    SELFTEST_RUN(test_hal_clock);
    SELFTEST_RUN(test_block_ramdisk_module);
    SELFTEST_RUN(test_hfs_volume_mounted);
    SELFTEST_RUN(test_ata_disk);
    SELFTEST_RUN(test_m68k_guest_ram_size);
    SELFTEST_RUN(test_m68k_guest_program);
    SELFTEST_RUN(test_m68k_guest_divide_64);
    SELFTEST_RUN(test_m68k_guest_fline_exception);
    SELFTEST_RUN(test_user_programs);

    selftest_arch_run();
    selftest_platform_run();

    if (selftest_failures == 0)
    {
        kernel_printf("[TEST] all passed\n");
        selftest_platform_exit(1);
    }

    kernel_printf("[TEST] %d failed\n", selftest_failures);
    selftest_platform_exit(0);
}

#endif

/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "process.h"

#include "console.h"
#include "elf.h"
#include "stack.h"

#include "hal/boot.h"
#include "hal/mmu.h"
#include "hal/usermode.h"
#include "hal/timer.h"

#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/log/log.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"
#include "kernel/printf/printf.h"
#include "kernel/vfs/hfsfs.h"
#include "kernel/vfs/ramfs.h"
#include "kernel/vfs/tarfs.h"

user_process_t user_process;

#define PAGE HAL_MMU_PAGE_SIZE
#define MAX_PROGRAM_SIZE (64u * 1024 * 1024)

/* --- memory --- */

static uint32_t page_down(uint32_t a)
{
    return a & ~(PAGE - 1);
}

static uint32_t page_up(uint32_t a)
{
    return (a + PAGE - 1) & ~(PAGE - 1);
}

/* Map one fresh zeroed page. */
static int32_t map_page(uint32_t virt)
{
    uintptr_t frame = pmm_alloc_frame();

    if (frame == 0)
        return -K_ENOMEM;

    if (hal_mmu_map(virt, frame, HAL_MMU_WRITABLE | HAL_MMU_USER) != 0)
    {
        pmm_free_frame(frame);

        return -K_ENOMEM;
    }

    k_memset((void *)virt, 0, PAGE);

    return 0;
}

static void unmap_page(uint32_t virt)
{
    uintptr_t phys;

    if (hal_mmu_get_phys(virt, &phys) != 0)
        return;

    hal_mmu_unmap(virt);
    pmm_free_frame(phys & ~(uintptr_t)(PAGE - 1));
}

int32_t user_map(uint32_t start, uint32_t end)
{
    if (user_regions_add(&user_process.regions, start, end) != 0)
        return -K_ENOMEM;

    for (uint32_t page = start; page < end; page += PAGE)
    {
        int32_t status = map_page(page);

        if (status != 0)
        {
            user_unmap(start, page);
            user_regions_remove(&user_process.regions, page, end);

            return status;
        }
    }

    return 0;
}

void user_unmap(uint32_t start, uint32_t end)
{
    for (uint32_t page = start; page < end; page += PAGE)
        unmap_page(page);

    user_regions_remove(&user_process.regions, start, end);
}

int user_range_ok(uint32_t address, uint32_t length)
{
    return user_regions_contains(&user_process.regions, address, length);
}

int32_t user_copy_string(char *buffer, size_t size, uint32_t address)
{
    for (size_t i = 0; i < size; i++)
    {
        if (!user_range_ok(address + (uint32_t)i, 1))
            return -K_EFAULT;

        buffer[i] = *(const char *)(uintptr_t)(address + i);

        if (buffer[i] == '\0')
            return (int32_t)i;
    }

    return -K_ENAMETOOLONG;
}

/* --- file descriptors --- */

int32_t user_fd_install(vfs_file_t *file, int min)
{
    for (int i = min; i < USER_MAX_FDS; i++)
    {
        if (user_process.fd[i] == NULL)
        {
            user_process.fd[i] = file;

            return i;
        }
    }

    return -K_EMFILE;
}

vfs_file_t *user_fd_get(int fd)
{
    if (fd < 0 || fd >= USER_MAX_FDS)
        return NULL;

    return user_process.fd[fd];
}

int32_t user_fd_close(int fd)
{
    vfs_file_t *file = user_fd_get(fd);

    if (file == NULL)
        return -K_EBADF;

    user_process.fd[fd] = NULL;
    vfs_file_close(file);

    return 0;
}

/* --- loading --- */

/* Map [start, end) except for the pages that are already part of the program. */
static int32_t map_missing(uint32_t start, uint32_t end)
{
    for (uint32_t page = start; page < end; page += PAGE)
    {
        if (user_range_ok(page, 1))
            continue;

        int32_t status = user_map(page, page + PAGE);

        if (status != 0)
            return status;
    }

    return 0;
}

static int32_t read_whole_file(const char *path, void **data, size_t *size)
{
    vfs_stat_t stat;
    int32_t status = vfs_stat(path, &stat);

    if (status != 0)
        return status;

    if ((stat.mode & VFS_S_IFMT) != VFS_S_IFREG)
        return -K_EACCES;

    if (stat.size == 0 || stat.size > MAX_PROGRAM_SIZE)
        return -K_EFBIG;

    vfs_file_t *file;

    status = vfs_open(path, VFS_O_RDONLY, 0, &file);

    if (status != 0)
        return status;

    uint8_t *buffer = kmalloc((size_t)stat.size);

    if (buffer == NULL)
    {
        vfs_file_close(file);

        return -K_ENOMEM;
    }

    size_t have = 0;

    while (have < stat.size)
    {
        int32_t n = vfs_read(file, buffer + have, (uint32_t)(stat.size - have));

        if (n <= 0)
            break;

        have += (size_t)n;
    }

    vfs_file_close(file);

    if (have != stat.size)
    {
        kfree(buffer);

        return -K_EIO;
    }

    *data = buffer;
    *size = have;

    return 0;
}

static void cleanup(void)
{
    for (int i = 0; i < USER_MAX_FDS; i++)
        user_fd_close(i);

    /* Free the pages region by region; user_unmap edits the list as it goes. */
    while (user_process.regions.count > 0)
    {
        user_region_t r = user_process.regions.region[0];

        user_unmap(r.start, r.end);
    }

    user_process.running = 0;
}

static int32_t load_image(const uint8_t *file, const elf_image_t *image)
{
    uint32_t image_end = 0;

    for (unsigned int i = 0; i < image->segment_count; i++)
    {
        const elf_segment_t *seg = &image->segments[i];
        uint32_t end = seg->vaddr + seg->memsz;

        if (seg->vaddr < USER_IMAGE_MIN || end > USER_BRK_LIMIT)
            return -K_ENOMEM;

        int32_t status = map_missing(page_down(seg->vaddr), page_up(end));

        if (status != 0)
            return status;

        /* The pages are zero already, so only the file part is copied. */
        k_memcpy((void *)(uintptr_t)seg->vaddr, file + seg->offset, seg->filesz);

        if (page_up(end) > image_end)
            image_end = page_up(end);
    }

    user_process.brk_start = image_end;
    user_process.brk = image_end;
    user_process.mmap_next = USER_MMAP_BASE;

    return 0;
}

static int32_t build_stack(const elf_image_t *image, const char *const *argv,
                           const char *const *envp, uint32_t *sp)
{
    int32_t status = user_map(USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP);

    if (status != 0)
        return status;

    enum { CAPACITY = 32768 };
    uint8_t *buffer = kmalloc(CAPACITY);

    if (buffer == NULL)
        return -K_ENOMEM;

    user_stack_params_t params;

    k_memset(&params, 0, sizeof(params));
    params.phdr = image->phdr_vaddr;
    params.phent = image->phent;
    params.phnum = image->phnum;
    params.entry = image->entry;

    /* AT_RANDOM: derived from the clock; programs use it for hash seeds only. */
    uint64_t seed = hal_timer_ns() * 6364136223846793005ull + 1442695040888963407ull;

    for (int i = 0; i < 16; i++)
    {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        params.random[i] = (uint8_t)(seed >> 33);
    }

    size_t used = user_stack_build(buffer, CAPACITY, USER_STACK_TOP, argv, envp, &params, sp);

    if (used == 0)
    {
        kfree(buffer);

        return -K_ENOMEM; /* arguments too big (E2BIG in Linux) */
    }

    k_memcpy((void *)(uintptr_t)(USER_STACK_TOP - used), buffer + CAPACITY - used, used);
    kfree(buffer);

    return 0;
}

int32_t user_run(const char *path, const char *const *argv, const char *const *envp)
{
    if (user_process.running)
        return -K_EBUSY;

    void *file;
    size_t size;
    int32_t status = read_whole_file(path, &file, &size);

    if (status != 0)
        return status;

    elf_image_t image;
    const char *error = NULL;

    if (elf_parse(file, size, &image, &error) != 0)
    {
        kernel_printf("%s: %s\n", path, error);
        kfree(file);

        return -K_EACCES;
    }

    k_memset(&user_process, 0, sizeof(user_process));
    user_process.running = 1;

    for (size_t i = 0; i < sizeof(user_process.name) - 1 && path[i] != '\0'; i++)
        user_process.name[i] = path[i];

    uint32_t sp = 0;

    status = load_image(file, &image);
    kfree(file);

    if (status == 0)
        status = build_stack(&image, argv, envp, &sp);

    vfs_file_t *in = console_file_open(VFS_O_RDONLY);
    vfs_file_t *out = console_file_open(VFS_O_WRONLY);
    vfs_file_t *err = console_file_open(VFS_O_WRONLY);

    if (status == 0 && (in == NULL || out == NULL || err == NULL))
        status = -K_ENOMEM;

    user_process.fd[0] = in;
    user_process.fd[1] = out;
    user_process.fd[2] = err;

    if (status != 0)
    {
        cleanup();

        return status;
    }

    int exit_status = hal_user_run(image.entry, sp);

    cleanup();

    return exit_status & 0xFF;
}

/* --- start-up --- */

/* A fault ends the program like a fatal signal: status 128 + signal number. */
static void on_fault(const hal_user_fault_t *fault)
{
    int signal = 11; /* SIGSEGV */
    const char *what = "segmentation fault";

    switch (fault->vector)
    {
    case 0:
        signal = 8;
        what = "divide error";
        break;

    case 6:
        signal = 4;
        what = "illegal instruction";
        break;

    case 7:
    case 16:
    case 19:
        signal = 8;
        what = "floating point exception";
        break;

    case 14:
        break;

    default:
        what = "protection fault";
        break;
    }

    kernel_printf("%s: %s at %x (eip %x)\n", user_process.name, what,
                  fault->address, fault->eip);
    hal_user_exit(128 + signal);
}

static int32_t on_syscall(const hal_user_syscall_t *call)
{
    return user_syscall(call);
}

static void mount_or_warn(const char *path, vfs_fs_t *fs)
{
    if (fs == NULL || vfs_mount(path, fs) != 0)
        kernel_log(LOG_WARN, "cannot mount a file system");
}

/*
 * The boot loader's string for a module is "<file> <name>" (QEMU) or
 * the line after "module" (GRUB): the module is the one whose last word
 * is name.
 */
static int module_is_named(const char *string, const char *name)
{
    size_t wanted = k_strlen(name);
    size_t length = k_strlen(string);

    return length >= wanted && k_strcmp(string + length - wanted, name) == 0 &&
           (length == wanted || string[length - wanted - 1] == ' ');
}

void user_init(void)
{
    hal_user_init(on_syscall, on_fault);

    /* "/" is the program disk if the boot loader gave one, else an empty RAM disk. */
    vfs_fs_t *root = NULL;
    hal_boot_module_t module;

    for (unsigned int i = 0; hal_boot_module(i, &module) == 0; i++)
    {
        if (module_is_named(module.name, "initrd"))
        {
            root = tarfs_create((const void *)(uintptr_t)module.base, (size_t)module.length);

            if (root == NULL)
                kernel_log(LOG_WARN, "initrd is not a tar archive");

            break;
        }
    }

    mount_or_warn("/", root != NULL ? root : ramfs_create());
    mount_or_warn("/tmp", ramfs_create());
    mount_or_warn("/dev", devfs_create());
    mount_or_warn("/hfs", hfsfs_create());
}

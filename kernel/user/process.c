/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "process.h"

#include "console.h"
#include "sched.h"
#include "signal.h"
#include "fbdev.h"
#include "procfiles.h"
#include "elf.h"
#include "stack.h"

#include "hal/boot.h"
#include "kernel/boot/module.h"
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

static int is_device_page(uint32_t virt)
{
    for (unsigned int i = 0; i < user_process.device_count; i++)
    {
        if (virt >= user_process.device[i].start && virt < user_process.device[i].end)
            return 1;
    }

    return 0;
}

static void unmap_page(uint32_t virt)
{
    uintptr_t phys;

    if (hal_mmu_get_phys(virt, &phys) != 0)
        return;

    hal_mmu_unmap(virt);

    /* Device memory is not ours to free. */
    if (!is_device_page(virt))
        pmm_free_frame(phys & ~(uintptr_t)(PAGE - 1));
}

int user_demand_map(uint32_t address)
{
    uint32_t page = page_down(address);

    if (page < USER_STACK_TOP - USER_STACK_SIZE || page >= USER_STACK_TOP - USER_STACK_EAGER ||
        !user_regions_contains(&user_process.regions, page, PAGE))
        return 0;

    uintptr_t phys;

    if (hal_mmu_get_phys(page, &phys) == 0)
        return 0; /* mapped already: the fault has another reason (write to a read-only page...) */

    return map_page(page) == 0;
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

int32_t user_map_device(uint32_t start, uint32_t size, uintptr_t phys)
{
    uint32_t end = start + size;

    if (user_process.device_count == sizeof(user_process.device) / sizeof(user_process.device[0]))
        return -K_ENOMEM;

    if (user_regions_add(&user_process.regions, start, end) != 0)
        return -K_ENOMEM;

    /* Recorded first: user_unmap must not free these frames, whatever happens next. */
    user_process.device[user_process.device_count].start = start;
    user_process.device[user_process.device_count].end = end;
    user_process.device_count++;

    for (uint32_t offset = 0; offset < size; offset += PAGE)
    {
        if (hal_mmu_map(start + offset, phys + offset, HAL_MMU_WRITABLE | HAL_MMU_USER) != 0)
        {
            user_unmap(start, start + offset);
            user_regions_remove(&user_process.regions, start + offset, end);

            return -K_ENOMEM;
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

/* True if every page that [address, address + length) touches is mapped with at least the flags in need. */
static int pages_allow(uint32_t address, uint32_t length, uint32_t need)
{
    if (length == 0)
        return 1;

    for (uint32_t page = page_down(address); page <= page_down(address + length - 1); page += PAGE)
    {
        uint32_t flags;

        if (hal_mmu_get_flags(page, &flags) != 0)
        {
            /* A stack page the program never touched: it exists once something asks for it. */
            if (!user_demand_map(page) || hal_mmu_get_flags(page, &flags) != 0)
                return 0;
        }

        if ((flags & need) != need)
            return 0;

        if (page + PAGE < page)
            break; /* the last page of the address space */
    }

    return 1;
}

int user_range_ok(uint32_t address, uint32_t length)
{
    return user_regions_contains(&user_process.regions, address, length) &&
           pages_allow(address, length, HAL_MMU_USER);
}

int user_range_writable(uint32_t address, uint32_t length)
{
    return user_regions_contains(&user_process.regions, address, length) &&
           pages_allow(address, length, HAL_MMU_USER | HAL_MMU_WRITABLE);
}

int32_t user_copy_string(char *buffer, size_t size, uint32_t address)
{
    for (size_t i = 0; i < size; i++)
    {
        /* Mapped and readable: checked on the first byte and whenever a new page starts. */
        if ((i == 0 || ((address + i) & (PAGE - 1)) == 0) && !user_range_ok(address + (uint32_t)i, 1))
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
    user_process.fd_cloexec[fd / 32] &= ~(1u << (fd % 32));
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

/* Close every descriptor and free every page of the running process. */
void user_release_memory(void)
{
    for (int i = 0; i < USER_MAX_FDS; i++)
        user_fd_close(i);

    user_unmap_all();
}

/* Free the pages region by region; user_unmap edits the list as it goes. */
void user_unmap_all(void)
{
    while (user_process.regions.count > 0)
    {
        user_region_t r = user_process.regions.region[0];

        user_unmap(r.start, r.end);
    }

    user_process.device_count = 0;
}

/* Release a process that is not the running one (a fork that failed half way). */
void user_discard(user_process_t *p)
{
    user_process_t *saved = user_current;
    hal_space_t saved_space = hal_space_current();

    user_current = p;
    hal_space_switch(p->space);
    user_release_memory();
    user_current = saved;
    hal_space_switch(saved_space);
}

int32_t user_copy_memory(user_process_t *child)
{
    user_process_t *parent = user_current;

    /* The regions first: user_discard frees what the copy got to, whatever happens. */
    child->regions = parent->regions;
    child->brk_start = parent->brk_start;
    child->brk = parent->brk;
    child->mmap_next = parent->mmap_next;
    child->device_count = parent->device_count;
    k_memcpy(child->device, parent->device, sizeof(child->device));

    for (unsigned int i = 0; i < parent->regions.count; i++)
    {
        for (uint32_t page = parent->regions.region[i].start; page < parent->regions.region[i].end; page += PAGE)
        {
            if (is_device_page(page))
            {
                uintptr_t phys;
                uint32_t flags;

                if (hal_mmu_get_phys(page, &phys) != 0 || hal_mmu_get_flags(page, &flags) != 0 ||
                    hal_space_map(child->space, page, phys & ~(uintptr_t)(PAGE - 1), flags) != 0)
                    return -K_ENOMEM;
            }
            else
            {
                uintptr_t phys;

                /* A stack page nobody has touched yet does not exist: the child gets none either. */
                if (hal_mmu_get_phys(page, &phys) != 0)
                    continue;

                if (hal_space_copy_page(child->space, page) != 0)
                    return -K_ENOMEM;
            }
        }
    }

    return 0;
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

    /*
     * Pages of segments without the write flag become read-only (code and constants
     * cannot be overwritten, a stray write faults). A page that two segments share
     * stays writable if either one is.
     */
    for (unsigned int i = 0; i < image->segment_count; i++)
    {
        const elf_segment_t *seg = &image->segments[i];

        if (seg->flags & ELF_PF_W)
            continue;

        for (uint32_t page = page_down(seg->vaddr); page < page_up(seg->vaddr + seg->memsz); page += PAGE)
        {
            int shared_writable = 0;

            for (unsigned int j = 0; j < image->segment_count; j++)
            {
                const elf_segment_t *other = &image->segments[j];

                if ((other->flags & ELF_PF_W) && page < page_up(other->vaddr + other->memsz) &&
                    page_down(other->vaddr) <= page)
                    shared_writable = 1;
            }

            if (!shared_writable)
                hal_mmu_protect(page, HAL_MMU_USER);
        }
    }

    user_process.brk_start = image_end;
    user_process.brk = image_end;
    user_process.mmap_next = USER_MMAP_BASE;

    return 0;
}

static int32_t build_stack(const elf_image_t *image, const char *const *argv,
                           const char *const *envp, uint32_t *sp)
{
    /* The whole stack is the program's (a region), but only its top pages exist yet (user_demand_map does the rest). */
    if (user_regions_add(&user_process.regions, USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP) != 0)
        return -K_ENOMEM;

    for (uint32_t page = USER_STACK_TOP - USER_STACK_EAGER; page < USER_STACK_TOP; page += PAGE)
    {
        int32_t status = map_page(page);

        if (status != 0)
            return status;
    }

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

/* Open the descriptors 0, 1 and 2 of a new process: the console unless the caller gives a file. */
static int32_t standard_files(user_process_t *p, vfs_file_t *const files[3])
{
    for (int i = 0; i < 3; i++)
    {
        vfs_file_t *file = files != NULL ? files[i] : NULL;

        if (file != NULL)
            p->fd[i] = vfs_file_ref(file);
        else
            p->fd[i] = console_file_open(i == 0 ? VFS_O_RDONLY : VFS_O_WRONLY);

        if (p->fd[i] == NULL)
            return -K_ENOMEM;
    }

    return 0;
}

int32_t user_spawn(const char *path, const char *const *argv, const char *const *envp,
                   vfs_file_t *const files[3], int join_group)
{
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

    user_process_t *parent = user_current;
    user_process_t *child = sched_create(parent);

    if (child == NULL)
    {
        kfree(file);

        return -K_EAGAIN;
    }

    if (join_group > 0)
        child->pgid = join_group;

    for (size_t i = 0; i < sizeof(child->name) - 1 && path[i] != '\0'; i++)
        child->name[i] = path[i];

    child->name[sizeof(child->name) - 1] = '\0';

    status = standard_files(child, files);

    /* Fill the new address space from here: make it the current one for a moment. */
    uint32_t sp = 0;
    hal_space_t saved_space = hal_space_current();

    if (status == 0)
    {
        user_current = child;
        hal_space_switch(child->space);

        status = load_image(file, &image);

        if (status == 0)
            status = build_stack(&image, argv, envp, &sp);

        if (status != 0)
            user_release_memory();

        user_current = parent;
        hal_space_switch(saved_space);
    }

    kfree(file);

    if (status != 0)
    {
        user_discard(child);
        sched_destroy(child);

        return status;
    }

    sched_start(child, image.entry, sp);

    return child->pid;
}

int32_t user_run(const char *path, const char *const *argv, const char *const *envp)
{
    return user_run_redirect(path, argv, envp, NULL, NULL, 0);
}

int32_t user_run_redirect(const char *path, const char *const *argv, const char *const *envp,
                          const char *stdin_path, const char *stdout_path, int append)
{
    user_job_stage_t stage = {path, argv, stdin_path, stdout_path, append};
    int32_t status;
    int32_t result = user_run_job(&stage, 1, envp, &status);

    return result != 0 ? result : status;
}

/* The status of a collected child as user_run reports it: the exit status, or 128 + the signal. */
static int32_t decode_status(int wait_status)
{
    if ((wait_status & 0x7F) != 0)
        return 128 + (wait_status & 0x7F);

    return (wait_status >> 8) & 0xFF;
}

/* End every process that is still alive (the job's leftovers) and collect all that ended. */
static void reap_leftovers(void)
{
    user_process_t *self = user_current;

    for (;;)
    {
        int alive = 0;

        for (unsigned int i = 1; i <= USER_MAX_PROCS; i++)
        {
            user_process_t *p = sched_slot(i);

            if (p != NULL && p->state != PROC_ZOMBIE)
            {
                signal_send(p, SIGNAL_KILL);
                alive++;
            }
        }

        if (alive == 0)
            break;

        sched_wait();
    }

    for (;;)
    {
        int status;

        if (sched_wait_child(-1, &status, 1) <= 0)
            break;
    }

    (void)self;
}

int32_t user_run_job(const user_job_stage_t *stages, unsigned int count, const char *const *envp,
                     int32_t *status)
{
    if (count == 0 || count > USER_JOB_MAX_STAGES)
        return -K_EINVAL;

    int pids[USER_JOB_MAX_STAGES];
    vfs_file_t *pipe_read = NULL; /* the read end that feeds the next stage */
    int32_t result = 0;
    unsigned int started = 0;
    int group = 0;

    user_console_reset();

    for (unsigned int i = 0; i < count && result == 0; i++)
    {
        vfs_file_t *files[3] = {NULL, NULL, NULL};
        vfs_file_t *next_read = NULL;

        /* Input: a file, else the pipe from the stage before, else the console. */
        if (stages[i].stdin_path != NULL)
            result = vfs_open(stages[i].stdin_path, VFS_O_RDONLY, 0, &files[0]);
        else if (pipe_read != NULL)
            files[0] = vfs_file_ref(pipe_read);

        /* Output: a file, else a pipe to the next stage, else the console. */
        if (result == 0 && stages[i].stdout_path != NULL)
        {
            result = vfs_open(stages[i].stdout_path,
                              VFS_O_WRONLY | VFS_O_CREAT | (stages[i].append ? VFS_O_APPEND : VFS_O_TRUNC),
                              0644, &files[1]);
        }
        else if (result == 0 && i + 1 < count)
        {
            result = pipe_create(&next_read, &files[1]);
        }

        if (result == 0)
        {
            result = user_spawn(stages[i].path, stages[i].argv, envp, files, group);

            if (result > 0)
            {
                pids[started++] = result;

                if (group == 0)
                {
                    group = result;
                    user_console_set_foreground(group);
                }

                result = 0;
            }
        }

        for (int k = 0; k < 3; k++)
        {
            if (files[k] != NULL)
                vfs_file_close(files[k]);
        }

        if (pipe_read != NULL)
            vfs_file_close(pipe_read);

        pipe_read = next_read;
    }

    if (pipe_read != NULL)
        vfs_file_close(pipe_read);

    /* A stage that could not start ends the job: the ones that did are stopped. */
    if (result != 0)
    {
        for (unsigned int i = 0; i < started; i++)
            signal_send(sched_find(pids[i]), SIGNAL_KILL);
    }

    for (unsigned int i = 0; i < count; i++)
        status[i] = 0;

    for (unsigned int i = 0; i < started; i++)
    {
        int wait_status = 0;

        /* While the job runs, keep reading the keyboard: Ctrl-C is a signal to the job. */
        while (sched_wait_child(pids[i], &wait_status, 1) == 0)
        {
            user_console_pump();
            sched_wait();
        }

        status[i] = decode_status(wait_status);
    }

    if (result != 0)
        status[started] = result;

    reap_leftovers();
    user_console_set_foreground(0);
    user_console_reset();
    fbdev_program_ended();

    return 0;
}

int32_t user_exec(const char *path, const char *const *argv, const char *const *envp)
{
    void *file;
    size_t size;
    int32_t status = read_whole_file(path, &file, &size);

    if (status != 0)
        return status;

    elf_image_t image;
    const char *error = NULL;

    if (elf_parse(file, size, &image, &error) != 0)
    {
        kfree(file);

        return -K_ENOEXEC;
    }

    /* Point of no return: the old program's memory goes. */
    user_process_t *p = user_current;

    user_unmap_all();

    for (int i = 0; i < USER_MAX_FDS; i++)
    {
        if (p->fd_cloexec[i / 32] & (1u << (i % 32)))
            user_fd_close(i);
    }

    uint32_t sp = 0;

    status = load_image(file, &image);
    kfree(file);

    if (status == 0)
        status = build_stack(&image, argv, envp, &sp);

    if (status != 0)
        sched_exit(0, SIGNAL_SEGV);

    signal_reset_for_exec(p);
    p->tls_base = 0;
    hal_task_set_tls(0);

    uint8_t fresh[HAL_FPU_AREA_SIZE + 16];
    void *area = (void *)(((uintptr_t)fresh + 15) & ~(uintptr_t)15);

    hal_fpu_reset_area(area);
    hal_fpu_restore(area);

    for (size_t i = 0; i < sizeof(p->name) - 1 && path[i] != '\0'; i++)
        p->name[i] = path[i];

    hal_user_regs_t regs;

    k_memset(&regs, 0, sizeof(regs));
    regs.eip = image.entry;
    regs.esp = sp;
    regs.eflags = 0x202;
    hal_user_regs_set(&regs);

    return 0;
}

/* --- start-up --- */

/* A fault ends the program like a fatal signal: status 128 + signal number. */
static int on_fault(const hal_user_fault_t *fault)
{
    /* A touch of a stack page that does not exist yet (page not present, error bit 0 clear) is not an error. */
    if (fault->vector == 14 && !(fault->error & 1) && user_demand_map(fault->address))
        return 1;

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
    sched_exit(0, signal);
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

void user_init(void)
{
    hal_user_init(on_syscall, on_fault);
    sched_init();

    /* "/" is the program disk if the boot loader gave one, else an empty RAM disk. */
    vfs_fs_t *root = NULL;
    hal_boot_module_t module;

    if (boot_module_find("initrd", &module) != NULL)
    {
        root = tarfs_create((const void *)(uintptr_t)module.base, (size_t)module.length);

        if (root == NULL)
            kernel_log(LOG_WARN, "initrd is not a tar archive");
    }

    mount_or_warn("/", root != NULL ? root : ramfs_create());
    mount_or_warn("/tmp", ramfs_create());
    mount_or_warn("/dev", devfs_create());
    mount_or_warn("/proc", procfiles_create());
    mount_or_warn("/hfs", hfsfs_create());
}

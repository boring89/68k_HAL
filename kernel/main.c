/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "main.h"

#include "kernel/console/console.h"
#include "kernel/console/history.h"
#include "kernel/terminal/terminal.h"
#include "kernel/terminal/input.h"
#include "kernel/shell/command.h"
#include "kernel/shell/complete.h"
#include "kernel/shell/shell.h"

#include "kernel/lib/k_string.h"
#include "kernel/log/log.h"
#include "kernel/printf/printf.h"

#include "kernel/block/cache.h"
#include "kernel/block/ramdisk.h"
#include "kernel/boot/module.h"
#include "kernel/config/config.h"
#include "kernel/fs/macdisk.h"
#include "kernel/fs/volume.h"

#include "kernel/input/event_buffer.h"

#include "kernel/keyboard/state.h"
#include "kernel/keyboard/keyseq.h"

#include "kernel/mac/machine.h"
#include "kernel/net/net.h"
#include "kernel/usb/usb.h"
#include "kernel/user/process.h"
#include "kernel/memory/memory.h"
#include "kernel/memory/heap.h"
#include "kernel/memory/pmm.h"

#include "kernel/display/fb_pattern.h"
#include "kernel/display/fbcon.h"
#include "kernel/display/text_display.h"
#include "kernel/panic/panic.h"

#include "hal/boot.h"
#include "hal/cpu.h"
#include "hal/framebuffer.h"
#include "hal/init.h"
#include "hal/mmu.h"
#include "hal/power.h"
#include "hal/timer.h"

#ifdef KERNEL_SELFTEST
#include "kernel/test/selftest.h"
#endif

#include <stdint.h>

/*
 * The settings: 68k_hal.cfg (the boot module "config", put there by the Boot partition's GRUB menu), then
 * the key=value words of the kernel command line.
 */
static void load_config(void)
{
    hal_boot_module_t module;

    config_reset();

    if (boot_module_find("config", &module) != NULL)
        kernel_printf("config: %d settings from 68k_hal.cfg\n",
                      config_parse((const char *)hal_phys_to_virt(module.base), (size_t)module.length));

    config_apply_cmdline(hal_boot_cmdline());
}

static void append(char *line, size_t size, const char *text)
{
    size_t length = k_strlen(line);

    if (length + k_strlen(text) < size)
        k_strcpy(line + length, text);
}

/*
 * The disk cache (kernel/block/cache.h): its line buffers. The 64-bit kernel reaches all RAM through the
 * direct map, so they are PMM frames; the 32-bit kernel only has its heap.
 */
static void *cache_line_alloc(void)
{
#ifdef HAL_PHYS_BASE
    uintptr_t frames = pmm_alloc_frames(BLOCK_CACHE_LINE_BYTES / HAL_MMU_PAGE_SIZE);

    return frames != 0 ? hal_phys_to_virt(frames) : NULL;
#else
    return kmalloc(BLOCK_CACHE_LINE_BYTES);
#endif
}

#ifdef HAL_PHYS_BASE
#define DISK_CACHE_MAX_MB 8192u     /* the default's ceiling: 8 GiB */
#else
#define DISK_CACHE_MAX_MB 32u       /* the 32-bit heap is small */
#endif

/* disk_cache = MB (0 = off) or auto: a quarter of the free memory, at most DISK_CACHE_MAX_MB. */
static void start_disk_cache(void)
{
    const char *setting = config_get_or("disk_cache", "auto");
    uint64_t free_mb = (uint64_t)pmm_get_free_frames() * HAL_MMU_PAGE_SIZE / (1024 * 1024);
    uint64_t mb = free_mb / 4 < DISK_CACHE_MAX_MB ? free_mb / 4 : DISK_CACHE_MAX_MB;

    if (k_strcmp(setting, "auto") != 0)
    {
        mb = 0;

        for (const char *p = setting; *p >= '0' && *p <= '9'; p++)
            mb = mb * 10 + (uint64_t)(*p - '0');

        if (mb > free_mb / 2)
            mb = free_mb / 2;   /* never more than half of the memory */
    }

    if (block_cache_init(mb * 1024 * 1024, cache_line_alloc) != 0)
        kernel_log(LOG_WARN, "disk cache: no memory for its table");
    else if (mb != 0)
        kernel_printf("disk cache: up to %u MB\n", (unsigned int)mb);
}

/* The "mac" shell command the settings ask for (autostart=mac). */
static void mac_command_from_config(char *line, size_t size)
{
    static const struct
    {
        const char *key;
        const char *arg;
    } keys[] = {{"mac_scale", " scale="}, {"mac_res", " res="}, {"mac_depth", " depth="},
                {"mac_cards", " cards="}, {"mac_ram", " ram="}};

    k_strcpy(line, "mac");

    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
    {
        const char *value = config_get(keys[i].key);

        if (value != NULL && k_strlen(line) + k_strlen(keys[i].arg) + k_strlen(value) < size)
        {
            append(line, size, keys[i].arg);
            append(line, size, value);
        }
    }

    if (k_strcmp(config_get_or("mac_video", "card"), "builtin") == 0)
        append(line, size, " builtin");
}

/* Height of the colour bars above the graphics console. */
#define FB_BARS_HEIGHT 16

void kernel_main(void)
{
    hal_init_console();
    console_init();
    console_history_start();

    hal_init_early();

    memory_init();
    pmm_init(memory_get_kernel_start(), memory_get_kernel_end());

    kernel_printf(
        "PMM: %u / %u frames free\n",
        (unsigned int)pmm_get_free_frames(),
        (unsigned int)pmm_get_total_frames());
    kernel_printf("Kernel end: %x\n", memory_get_kernel_end());

    kernel_log(LOG_INFO, "Kernel Booting...");
    hal_init();

    /* Needs paging: the heap has its own virtual address range. */
    heap_vm_init();

    /* Boot modules (hal/boot.h) become block devices ram0, ram1, ... */
    ramdisk_register_boot_modules();

    /* The first HFS volume among them is what the ls and cat commands read. */
    if (volume_mount_any() == 0)
        kernel_log(LOG_INFO, "HFS volume mounted");

    load_config();
    start_disk_cache();

    /* A 68k_HAL GPT disk (docs/mac_disk.md): its partitions become block devices and the system overlay opens. */
    if (macdisk_init(macdisk_slot_from_name(config_get("system_slot"))) == 0)
    {
        kernel_printf("macdisk: 68k_HAL disk, System %c, overlay %s, kernel slot %s\n",
                      macdisk_slot() == MACDISK_SLOT_A ? 'A' : 'B',
                      overlay_status_name(macdisk_overlay_status()), config_get_or("kernel_slot", "?"));

        if (macdisk_overlay_status() == OVERLAY_MISMATCH)
            kernel_log(LOG_WARN, "macdisk: the system overlay belongs to another System image and is not applied; 'macsys reset' starts a new one");
        else if (macdisk_overlay_status() != OVERLAY_OK)
            kernel_log(LOG_ERROR, "macdisk: the system overlay cannot be opened");
    }

    /* The file tree ("/", /tmp, /dev) and the mode switch for user programs. */
    user_init();

    /* Graphics mode: draw the text console on the framebuffer, under a strip of colour bars. */
    hal_framebuffer_t framebuffer;

    if (hal_framebuffer_get(&framebuffer) == 0)
    {
        kernel_printf("Framebuffer: %ux%u\n", framebuffer.width, framebuffer.height);

        /* What a slow screen needs (hal/mmu.h): "write combining" is the fast one; the report of a real machine shows if it worked. */
        uint32_t memory_type = 0;

        if (hal_mmu_get_flags((uintptr_t)framebuffer.base, &memory_type) == 0)
            kernel_printf("Framebuffer memory: %s\n", memory_type & HAL_MMU_WRITE_COMBINING ? "write combining"
                                                       : memory_type & HAL_MMU_UNCACHED      ? "uncached"
                                                                                             : "cached");

        if (text_display_get() == NULL && fbcon_init(&framebuffer, FB_BARS_HEIGHT) == 0 &&
            text_display_register(fbcon_display()) == 0)
        {
            console_init();
            fb_draw_color_bars(&framebuffer, FB_BARS_HEIGHT);
            kernel_printf("Console: %d x %d cells\n", fbcon_display()->width, fbcon_display()->height);
        }
        else
        {
            fb_draw_test_pattern(&framebuffer);
        }
    }

    input_event_buffer_init();

    terminal_input_init();
    terminal_input_set_callback(shell_execute);
    shell_complete_set_commands(shell_command_count, shell_command_name);
    terminal_input_set_completer(shell_complete);
    terminal_input_set_prompt(SHELL_PROMPT);
    keyboard_state_init();

    hal_cpu_enable_interrupts();
    kernel_log(LOG_INFO, "Shell ready");

#ifdef KERNEL_MAC_TEST
    /* make test-rom-qemu: run the Mac for a fixed time and report the screen. */
#ifdef KERNEL_MAC_TEST_VIDEO_W
    hal_framebuffer_set_mode(KERNEL_MAC_TEST_VIDEO_W, KERNEL_MAC_TEST_VIDEO_H);
#endif
#ifdef KERNEL_MAC_TEST_SCALE
    mac_machine_set_scale_halves(KERNEL_MAC_TEST_SCALE * 2);
#endif
#ifdef KERNEL_MAC_TEST_BUILTIN
    mac_machine_set_builtin(true);
#endif
#ifdef KERNEL_MAC_TEST_CARDS
    mac_machine_set_cards(KERNEL_MAC_TEST_CARDS);
#endif
#ifdef KERNEL_MAC_TEST_DEPTH
    mac_machine_set_depth(KERNEL_MAC_TEST_DEPTH);
#endif
#ifdef KERNEL_MAC_TEST_HALVES
    mac_machine_set_scale_halves(KERNEL_MAC_TEST_HALVES);
#endif
#ifdef KERNEL_MAC_TEST_WIDTH
    mac_machine_set_resolution(KERNEL_MAC_TEST_WIDTH, KERNEL_MAC_TEST_HEIGHT);
#else
    mac_machine_set_resolution(640, 480);
#endif
    if (mac_machine_run(KERNEL_MAC_TEST_FRAMES) == 0)
        kernel_printf("mac: screen crc %x after %u frames\n",
                      mac_machine_screen_crc(),
                      (unsigned int)KERNEL_MAC_TEST_FRAMES);
    else
        kernel_printf("mac: could not run\n");
    while (1)
        hal_cpu_idle();
#endif

#ifdef KERNEL_SELFTEST
    selftest_run();
#endif

    /* The prompt comes last, after every boot message, so typing starts on
     * the prompt's line. */
    /* "diag" on the kernel command line (the diagnostics entry of the USB image's menu). */
    const char *command_line = hal_boot_cmdline();

    if (command_line != 0 && k_strstr(command_line, "diag") != 0)
        shell_run_diagnostics();

    /* The kernel came up: after a trial boot of a new kernel slot, that slot becomes the confirmed one. */
    if (macdisk_present())
    {
        const char *kernel_slot = config_get("kernel_slot");
        int confirmed = macdisk_confirm_kernel_slot(kernel_slot, k_strcmp(config_get_or("kernel_confirm", "0"), "1") == 0);

        if (confirmed == 1)
            kernel_printf("macdisk: kernel slot %s works: it is the confirmed slot now\n", kernel_slot);
        else if (confirmed < 0)
            kernel_log(LOG_WARN, "macdisk: cannot write the confirmed kernel slot to grubenv");
    }

    /* autostart=mac: straight into the Mac; the shell comes when it stops (` or a failure to start). */
    if (k_strcmp(config_get_or("autostart", "shell"), "mac") == 0)
    {
        char line[96];

        mac_command_from_config(line, sizeof line);
        kernel_printf("autostart: %s\n", line);
        shell_execute(line);
    }
    else
    {
        shell_init();
    }

    uint32_t last_tick = 0;

    while (1)
    {
        uint32_t tick = hal_timer_ticks();

        net_poll();
        usb_poll();

        /* The machine's power button turns it off, as in any system (in the Mac it asks the Mac first). */
        if (hal_power_button_pressed())
        {
            kernel_printf("%s\n", "Power button: turning the computer off");
            hal_power_off();
        }

        if (tick - last_tick >= hal_timer_frequency())
        {
            // kernel_printf("1 second\n");
            last_tick = tick;
        }

        input_event_t event;

        while (input_event_pop(&event) == 0)
        {
            if (event.type == INPUT_EVENT_KEYBOARD)
            {
                keyboard_state_update(
                    event.data.keyboard.key,
                    event.data.keyboard.pressed);

                const keyboard_state_t *state =
                    keyboard_state_get();

                if (event.data.keyboard.pressed)
                {
                    /* The bytes a terminal would send: characters, and escape sequences for the arrow keys. */
                    char bytes[KEYBOARD_SEQUENCE_MAX];
                    int count = keyboard_key_sequence(event.data.keyboard.key, state, bytes);

                    for (int i = 0; i < count; i++)
                        terminal_input_char(bytes[i]);
                }
            }
        }

        hal_cpu_idle();
    }
}




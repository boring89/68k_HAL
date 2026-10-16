/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "console.h"

#include "kernel/console/console.h"
#include "kernel/input/event_buffer.h"
#include "kernel/keyboard/keyseq.h"
#include "kernel/keyboard/state.h"
#include "kernel/lib/k_errno.h"
#include "kernel/lib/k_memory.h"
#include "kernel/lib/k_string.h"
#include "kernel/memory/heap.h"
#include "kernel/terminal/ansi.h"
#include "kernel/terminal/terminal.h"
#include "fbdev.h"
#include "inputdev.h"
#include "process.h"

#include "hal/cpu.h"
#include "hal/timer.h"
#include "hal/usermode.h"

#include <stdbool.h>

/*
 * Line discipline of the console, configured like a Linux terminal
 * (struct termios, set with the TCGETS/TCSETS ioctls).
 *
 * Canonical mode (ICANON, the default): characters typed are echoed and
 * collected until Enter, then handed to read() in order. Backspace edits the
 * line that is being typed.
 *
 * Non-canonical ("raw") mode: every key is available to read() at once, as
 * the bytes of keyboard_key_sequence (arrows are escape sequences); VMIN and
 * VTIME say how long read() waits.
 */
#define LINE_MAX 256
#define RAW_QUEUE_SIZE 128

static char line[LINE_MAX];
static uint32_t line_length;   /* characters typed so far */
static uint32_t line_ready;    /* the line is complete: read() may take it */
static uint32_t line_consumed; /* how much of a complete line read() has taken */
static uint32_t line_eof;      /* Ctrl-D on an empty line: the next read() returns 0 (end of file) */

static uint8_t raw_queue[RAW_QUEUE_SIZE];
static uint32_t raw_head;
static uint32_t raw_count;

/* Linux asm-generic/termbits.h */
#define IGNCR 0x80u
#define INLCR 0x40u
#define ICRNL 0x100u
#define IXON 0x400u
#define OPOST 0x1u
#define ONLCR 0x4u
#define CS8 0x30u
#define CREAD 0x80u
#define HUPCL 0x400u
#define B38400 0xFu
#define ISIG 0x1u
#define ICANON 0x2u
#define ECHO 0x8u
#define ECHOE 0x10u
#define ECHOK 0x20u
#define ECHONL 0x40u
#define IEXTEN 0x8000u

#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSUSP 10
#define NCCS 19

#define TCGETS 0x5401u
#define TCSETS 0x5402u
#define TCSETSW 0x5403u
#define TCSETSF 0x5404u
#define TIOCGPGRP 0x540Fu
#define TIOCSPGRP 0x5410u
#define TIOCGWINSZ 0x5413u
#define TIOCSWINSZ 0x5414u
#define FIONREAD 0x541Bu

/* The kernel's struct termios on i386: 36 bytes. */
typedef struct
{
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t c_line;
    uint8_t c_cc[NCCS];
} termios_t;

typedef struct
{
    uint16_t rows;
    uint16_t columns;
    uint16_t x_pixels;
    uint16_t y_pixels;
} winsize_t;

static termios_t termios;

static void termios_defaults(void)
{
    k_memset(&termios, 0, sizeof(termios));
    termios.c_iflag = ICRNL | IXON;
    termios.c_oflag = OPOST | ONLCR;
    termios.c_cflag = B38400 | CS8 | CREAD | HUPCL;
    termios.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN;
    termios.c_cc[VINTR] = 3;   /* Ctrl-C */
    termios.c_cc[VQUIT] = 28;  /* Ctrl-\ */
    termios.c_cc[VERASE] = 127;
    termios.c_cc[VKILL] = 21;  /* Ctrl-U */
    termios.c_cc[VEOF] = 4;    /* Ctrl-D */
    termios.c_cc[VTIME] = 0;
    termios.c_cc[VMIN] = 1;
    termios.c_cc[VSUSP] = 26;
}

static void drop_input(void)
{
    line_length = 0;
    line_ready = 0;
    line_consumed = 0;
    line_eof = 0;
    raw_head = 0;
    raw_count = 0;
}

void user_console_reset(void)
{
    termios_defaults();
    drop_input();
    ansi_reset();
}

/* Echo a byte typed, if the terminal is set to. */
static void echo_byte(uint8_t c)
{
    if (termios.c_lflag & ECHO)
        console_write_char((char)c);
    else if (c == '\n' && (termios.c_lflag & ECHONL))
        console_write_char('\n');
}

static void canonical_byte(uint8_t c)
{
    /* A complete line waits for read(): typing more would edit text already handed out. */
    if (line_ready)
        return;

    /*
     * End of file (Ctrl-D): at the start of a line it is the end of the input; in the middle
     * it hands over what has been typed, without a newline (as on Unix terminals).
     */
    if (c == termios.c_cc[VEOF])
    {
        line_eof = line_length == 0;
        line_ready = 1;

        return;
    }

    if (c == termios.c_cc[VERASE] || c == '\b')
    {
        if (line_length > 0)
        {
            line_length--;

            if (termios.c_lflag & ECHOE)
                console_write_char('\b');
        }

        return;
    }

    if (c == termios.c_cc[VKILL])
    {
        while (line_length > 0)
        {
            line_length--;

            if (termios.c_lflag & ECHOE)
                console_write_char('\b');
        }

        return;
    }

    /* Other control characters do nothing here. */
    if (c < 0x20 && c != '\n' && c != '\t')
        return;

    if (line_length >= LINE_MAX - 1 && c != '\n')
        return;

    echo_byte(c);
    line[line_length++] = (char)c;

    if (c == '\n')
        line_ready = 1;
}

static void raw_byte(uint8_t c)
{
    if (raw_count == RAW_QUEUE_SIZE)
        return;

    raw_queue[(raw_head + raw_count++) % RAW_QUEUE_SIZE] = c;

    if (c >= 0x20 || c == '\n' || c == '\t')
        echo_byte(c);
}

/* One byte typed, after the signal and CR/NL translation of the input flags. */
static void feed_byte(uint8_t c)
{
    if (termios.c_lflag & ISIG)
    {
        if (c == termios.c_cc[VINTR] || c == termios.c_cc[VQUIT])
        {
            /* The program ends as if by SIGINT / SIGQUIT (there are no signal handlers for this). */
            if (termios.c_lflag & ECHO)
                console_write_line(c == termios.c_cc[VINTR] ? "^C" : "^\\");

            hal_user_exit(c == termios.c_cc[VINTR] ? 130 : 131);
        }
    }

    if (c == '\r')
    {
        if (termios.c_iflag & IGNCR)
            return;

        if (termios.c_iflag & ICRNL)
            c = '\n';
    }
    else if (c == '\n' && (termios.c_iflag & INLCR))
    {
        c = '\r';
    }

    if (termios.c_lflag & ICANON)
        canonical_byte(c);
    else
        raw_byte(c);
}

static bool input_full(void)
{
    return (termios.c_lflag & ICANON) ? line_ready != 0 : raw_count == RAW_QUEUE_SIZE;
}

/* Feed the keyboard events that have arrived to the line discipline, until it cannot take more. */
static void process_keys(void)
{
    input_event_t event;

    while (!input_full() && input_event_pop(&event) == 0)
    {
        if (event.type != INPUT_EVENT_KEYBOARD)
            continue;

        keyboard_state_update(event.data.keyboard.key, event.data.keyboard.pressed);

        if (!event.data.keyboard.pressed)
            continue;

        char bytes[KEYBOARD_SEQUENCE_MAX];
        int count = keyboard_key_sequence(event.data.keyboard.key, keyboard_state_get(), bytes);

        /* Arrows, function keys and the like have no meaning when editing a line. */
        if ((termios.c_lflag & ICANON) && count > 1 && bytes[0] == 0x1B)
            continue;

        for (int i = 0; i < count; i++)
            feed_byte((uint8_t)bytes[i]);
    }
}

static void wait_for_line(void)
{
    while (!line_ready)
    {
        process_keys();

        if (!line_ready)
        {
            hal_cpu_enable_interrupts();
            hal_cpu_idle();
        }
    }
}

static uint64_t now_ms(void)
{
    return hal_timer_ns() / 1000000u;
}

/*
 * Non-canonical read: wait as VMIN / VTIME say (termios(3)): VMIN bytes, or if VMIN is 0
 * the first byte, each limited by the VTIME tenths of a second when that is not 0.
 */
static void wait_for_raw(uint32_t count)
{
    uint32_t want = termios.c_cc[VMIN];
    uint64_t timeout_ms = (uint64_t)termios.c_cc[VTIME] * 100u;

    if (want > count)
        want = count;

    uint64_t last_change = now_ms();
    uint32_t last_count = raw_count;

    for (;;)
    {
        process_keys();

        if (raw_count != last_count)
        {
            last_count = raw_count;
            last_change = now_ms();
        }

        if (want == 0)
        {
            /* VMIN = 0: return what is there; with VTIME wait that long for the first byte. */
            if (raw_count > 0 || timeout_ms == 0 || now_ms() - last_change >= timeout_ms)
                return;
        }
        else if (raw_count >= want)
        {
            return;
        }
        else if (raw_count > 0 && timeout_ms != 0 && now_ms() - last_change >= timeout_ms)
        {
            /* VMIN > 0 with VTIME: the time between bytes. */
            return;
        }

        hal_cpu_enable_interrupts();
        hal_cpu_idle();
    }
}

static bool input_ready(void)
{
    if (termios.c_lflag & ICANON)
        return line_ready != 0;

    return raw_count >= (termios.c_cc[VMIN] > 0 ? termios.c_cc[VMIN] : 1);
}

/* poll(): the console can always be written; it can be read once a line (or, in raw mode, enough bytes) is there. */
static uint32_t console_poll(vfs_file_t *file, uint32_t events)
{
    (void)file;
    process_keys();

    return (events & VFS_POLLOUT) | ((events & VFS_POLLIN) && input_ready() ? VFS_POLLIN : 0);
}

static int32_t console_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    (void)file;
    (void)pos;

    if (count == 0)
        return 0;

    if (!(termios.c_lflag & ICANON))
    {
        wait_for_raw(count);

        uint32_t n = raw_count < count ? raw_count : count;
        uint8_t *out = buffer;

        for (uint32_t i = 0; i < n; i++)
        {
            out[i] = raw_queue[raw_head];
            raw_head = (raw_head + 1) % RAW_QUEUE_SIZE;
        }

        raw_count -= n;

        return (int32_t)n;
    }

    wait_for_line();

    if (line_eof)
    {
        line_eof = 0;
        line_ready = 0;
        line_length = 0;
        line_consumed = 0;

        return 0;
    }

    uint32_t available = line_length - line_consumed;

    if (count > available)
        count = available;

    k_memcpy(buffer, line + line_consumed, count);
    line_consumed += count;

    if (line_consumed == line_length)
    {
        line_length = 0;
        line_consumed = 0;
        line_ready = 0;
    }

    return (int32_t)count;
}

static int32_t console_ioctl(vfs_file_t *file, uint32_t request, uint32_t arg)
{
    (void)file;

    switch (request)
    {
    case TCGETS:
        if (!user_range_writable(arg, sizeof(termios)))
            return -K_EFAULT;

        k_memcpy((void *)(uintptr_t)arg, &termios, sizeof(termios));

        return 0;

    case TCSETS:
    case TCSETSW:
    case TCSETSF:
    {
        if (!user_range_ok(arg, sizeof(termios)))
            return -K_EFAULT;

        uint32_t old_lflag = termios.c_lflag;

        k_memcpy(&termios, (const void *)(uintptr_t)arg, sizeof(termios));

        /* TCSETSF throws away what was typed but not read. */
        if (request == TCSETSF)
            drop_input();
        else if ((old_lflag ^ termios.c_lflag) & ICANON)
            drop_input(); /* The text typed so far belongs to the other mode. */

        return 0;
    }

    case TIOCGWINSZ:
    {
        winsize_t size = {
            .rows = (uint16_t)terminal_height(),
            .columns = (uint16_t)terminal_width(),
        };

        if (!user_range_writable(arg, sizeof(size)))
            return -K_EFAULT;

        k_memcpy((void *)(uintptr_t)arg, &size, sizeof(size));

        return 0;
    }

    case TIOCSWINSZ:
        return 0;

    case TIOCGPGRP:
    {
        uint32_t group = 1;

        if (!user_range_writable(arg, sizeof(group)))
            return -K_EFAULT;

        k_memcpy((void *)(uintptr_t)arg, &group, sizeof(group));

        return 0;
    }

    case TIOCSPGRP:
        return 0;

    case FIONREAD:
    {
        process_keys();

        uint32_t bytes = (termios.c_lflag & ICANON) ? (line_ready ? line_length - line_consumed : 0) : raw_count;

        if (!user_range_writable(arg, sizeof(bytes)))
            return -K_EFAULT;

        k_memcpy((void *)(uintptr_t)arg, &bytes, sizeof(bytes));

        return 0;
    }

    default:
        return -K_ENOTTY;
    }
}

static char *capture_buffer;
static size_t capture_size;
static size_t capture_length;

void user_console_capture_start(char *buffer, size_t size)
{
    capture_buffer = buffer;
    capture_size = size;
    capture_length = 0;

    if (size > 0)
        buffer[0] = '\0';
}

size_t user_console_capture_stop(void)
{
    capture_buffer = NULL;

    return capture_length;
}

static int32_t console_write_bytes(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    const char *text = buffer;

    (void)file;
    (void)pos;

    for (uint32_t i = 0; i < count; i++)
    {
        ansi_put_char(text[i]);

        if (capture_buffer != NULL && capture_length + 1 < capture_size)
        {
            capture_buffer[capture_length++] = text[i];
            capture_buffer[capture_length] = '\0';
        }
    }

    return (int32_t)count;
}

static int32_t char_device_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    (void)file;
    stat->mode = VFS_S_IFCHR | 0666;
    stat->size = 0;
    stat->inode = 3;
    stat->mtime = 0;

    return 0;
}

static const vfs_file_ops_t console_ops = {
    .read = console_read,
    .write = console_write_bytes,
    .stat = char_device_stat,
    .poll = console_poll,
    .ioctl = console_ioctl,
};

static int32_t null_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    (void)file;
    (void)pos;
    (void)buffer;
    (void)count;

    return 0;
}

static int32_t null_write(vfs_file_t *file, uint64_t pos, const void *buffer, uint32_t count)
{
    (void)file;
    (void)pos;
    (void)buffer;

    return (int32_t)count;
}

static int32_t zero_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    (void)file;
    (void)pos;
    k_memset(buffer, 0, count);

    return (int32_t)count;
}

/* xorshift32; not secure, but programs only ask for hash seeds and such. */
static uint32_t random_state = 0x2545F491u;

static int32_t random_read(vfs_file_t *file, uint64_t pos, void *buffer, uint32_t count)
{
    uint8_t *bytes = buffer;

    (void)file;
    (void)pos;

    for (uint32_t i = 0; i < count; i++)
    {
        random_state ^= random_state << 13;
        random_state ^= random_state >> 17;
        random_state ^= random_state << 5;
        bytes[i] = (uint8_t)random_state;
    }

    return (int32_t)count;
}

static const vfs_file_ops_t null_ops = {
    .read = null_read, .write = null_write, .stat = char_device_stat};
static const vfs_file_ops_t zero_ops = {
    .read = zero_read, .write = null_write, .stat = char_device_stat};
static const vfs_file_ops_t random_ops = {
    .read = random_read, .write = null_write, .stat = char_device_stat};

vfs_file_t *console_file_open(uint32_t flags)
{
    vfs_file_t *file = vfs_file_new(&console_ops, NULL, flags);

    if (file != NULL)
        file->seekable = 0;

    return file;
}

/*
 * The devices of /dev. A name with a slash ("input/event0") can be opened but is not
 * listed (there are no directories in /dev). seekable: lseek works (the framebuffer).
 * available: NULL, or says whether the device exists on this machine (-ENODEV when not).
 */
static const struct
{
    const char *name;
    const vfs_file_ops_t *ops;
    int seekable;
    int (*available)(void);
} devices[] = {
    {"console", &console_ops, 0, NULL},
    {"tty", &console_ops, 0, NULL},
    {"null", &null_ops, 0, NULL},
    {"zero", &zero_ops, 0, NULL},
    {"urandom", &random_ops, 0, NULL},
    {"random", &random_ops, 0, NULL},
    {"fb0", &fbdev_ops, 1, fbdev_available},
    {"input/event0", &inputdev_ops, 0, NULL},
};

#define DEVICE_COUNT ((int)(sizeof(devices) / sizeof(devices[0])))

static int has_slash(const char *name)
{
    while (*name != '\0')
    {
        if (*name++ == '/')
            return 1;
    }

    return 0;
}

static int is_listed(int index)
{
    return !has_slash(devices[index].name) && (devices[index].available == NULL || devices[index].available());
}

/* The index-th listed device, or -1. */
static int listed_device(uint32_t index)
{
    for (int i = 0; i < DEVICE_COUNT; i++)
    {
        if (is_listed(i) && index-- == 0)
            return i;
    }

    return -1;
}

static int32_t dir_readdir(vfs_file_t *file, uint32_t index, vfs_dirent_t *entry)
{
    (void)file;

    if (index < 2)
    {
        k_strcpy(entry->name, index == 0 ? "." : "..");
        entry->inode = 1;
        entry->type = VFS_DT_DIR;

        return 0;
    }

    int device = listed_device(index - 2);

    if (device < 0)
        return 1;

    k_strcpy(entry->name, devices[device].name);
    entry->inode = (uint32_t)device + 2;
    entry->type = VFS_DT_CHR;

    return 0;
}

static int32_t dir_stat(vfs_file_t *file, vfs_stat_t *stat)
{
    (void)file;
    stat->mode = VFS_S_IFDIR | 0755;
    stat->size = 0;
    stat->inode = 1;
    stat->mtime = 0;

    return 0;
}

static const vfs_file_ops_t dir_ops = {
    .readdir = dir_readdir,
    .stat = dir_stat,
};

static int32_t devfs_open(vfs_fs_t *fs, const char *path, uint32_t flags,
                          uint32_t mode, vfs_file_t **file)
{
    (void)fs;
    (void)mode;

    if (path[0] == '\0')
    {
        if ((flags & VFS_O_ACCMODE) != VFS_O_RDONLY)
            return -K_EISDIR;

        *file = vfs_file_new(&dir_ops, NULL, flags);

        return *file != NULL ? 0 : -K_ENOMEM;
    }

    for (int i = 0; i < DEVICE_COUNT; i++)
    {
        if (k_strcmp(path, devices[i].name) == 0)
        {
            if (devices[i].available != NULL && !devices[i].available())
                return -K_ENODEV;

            *file = vfs_file_new(devices[i].ops, NULL, flags);

            if (*file == NULL)
                return -K_ENOMEM;

            (*file)->seekable = devices[i].seekable;

            if (devices[i].ops == &inputdev_ops)
                inputdev_flush();

            return 0;
        }
    }

    return -K_ENOENT;
}

static int32_t devfs_stat(vfs_fs_t *fs, const char *path, vfs_stat_t *stat)
{
    (void)fs;

    if (path[0] == '\0')
        return dir_stat(NULL, stat);

    for (int i = 0; i < DEVICE_COUNT; i++)
    {
        if (k_strcmp(path, devices[i].name) == 0)
            return char_device_stat(NULL, stat);
    }

    return -K_ENOENT;
}

vfs_fs_t *devfs_create(void)
{
    vfs_fs_t *fs = kmalloc(sizeof(*fs));

    if (fs == NULL)
        return NULL;

    k_memset(fs, 0, sizeof(*fs));
    fs->open = devfs_open;
    fs->stat = devfs_stat;

    return fs;
}

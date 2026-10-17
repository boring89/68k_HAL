/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * A toolbox of small commands in one program (the BusyBox idea): the
 * applet is chosen by the name the program is started as, so the shell runs
 * "/bin/box" with argv[0] = "cp" when you type cp. "box cp a b" and "run
 * /bin/box cp a b" work too, and "box --list" shows the applets. Exits 127
 * when there is no applet of that name (the shell then says "Unknown
 * command").
 *
 * Only what this system can do: the file tree has no permissions, links
 * or owners, and /tmp is the only place where files can be created.
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef int (*applet_fn)(int argc, char **argv);

static const char *program;

static int fail(const char *what)
{
    fprintf(stderr, "%s: %s: %s\n", program, what, strerror(errno));

    return 1;
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash != NULL ? slash + 1 : path;
}

/* Copy everything from one descriptor to another. Returns 0 or -1. */
static int copy_fd(int in, int out)
{
    char buffer[4096];

    for (;;)
    {
        ssize_t n = read(in, buffer, sizeof(buffer));

        if (n < 0)
            return -1;

        if (n == 0)
            return 0;

        for (ssize_t done = 0; done < n;)
        {
            ssize_t written = write(out, buffer + done, (size_t)(n - done));

            if (written < 0)
                return -1;

            done += written;
        }
    }
}

/* Option parsing: leading arguments like -a -l -n5; returns the index of the first operand. */
static int is_option(const char *arg)
{
    return arg[0] == '-' && arg[1] != '\0';
}

/* --- applets --- */

static int box_true(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    return 0;
}

static int box_false(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    return 1;
}

static int box_echo(int argc, char **argv)
{
    int first = 1;
    int newline = 1;

    if (argc > 1 && strcmp(argv[1], "-n") == 0)
    {
        newline = 0;
        first = 2;
    }

    for (int i = first; i < argc; i++)
        printf("%s%s", i > first ? " " : "", argv[i]);

    if (newline)
        printf("\n");

    return 0;
}

static int box_pwd(int argc, char **argv)
{
    char buffer[512];

    (void)argc;
    (void)argv;

    if (getcwd(buffer, sizeof(buffer)) == NULL)
        return fail("getcwd");

    printf("%s\n", buffer);

    return 0;
}

static int box_cat(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
        return copy_fd(0, 1) == 0 ? 0 : fail("read");

    for (int i = 1; i < argc; i++)
    {
        int fd = strcmp(argv[i], "-") == 0 ? 0 : open(argv[i], O_RDONLY);

        if (fd < 0)
        {
            status = fail(argv[i]);
            continue;
        }

        if (copy_fd(fd, 1) != 0)
            status = fail(argv[i]);

        if (fd != 0)
            close(fd);
    }

    return status;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void list_entry(const char *dir, const char *name, int long_format)
{
    if (!long_format)
    {
        printf("%s\n", name);

        return;
    }

    char path[1024];
    struct stat st;

    snprintf(path, sizeof(path), "%s/%s", dir, name);

    if (stat(path, &st) != 0)
    {
        printf("?          %s\n", name);

        return;
    }

    printf("%c %9lld %s%s\n", S_ISDIR(st.st_mode) ? 'd' : S_ISCHR(st.st_mode) ? 'c' : '-',
           (long long)st.st_size, name, S_ISDIR(st.st_mode) ? "/" : "");
}

static int list_path(const char *path, int all, int long_format, int show_name)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return fail(path);

    if (!S_ISDIR(st.st_mode))
    {
        printf("%s\n", path);

        return 0;
    }

    DIR *dir = opendir(path);

    if (dir == NULL)
        return fail(path);

    char **names = NULL;
    size_t count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (!all && entry->d_name[0] == '.')
            continue;

        char **grown = realloc(names, (count + 1) * sizeof(*names));

        if (grown == NULL)
            break;

        names = grown;
        names[count] = strdup(entry->d_name);

        if (names[count] == NULL)
            break;

        count++;
    }

    closedir(dir);
    qsort(names, count, sizeof(*names), compare_names);

    if (show_name)
        printf("%s:\n", path);

    for (size_t i = 0; i < count; i++)
    {
        list_entry(path, names[i], long_format);
        free(names[i]);
    }

    free(names);

    return 0;
}

static int box_ls(int argc, char **argv)
{
    int all = 0;
    int long_format = 0;
    int i = 1;
    int status = 0;

    for (; i < argc && is_option(argv[i]); i++)
    {
        for (const char *o = argv[i] + 1; *o != '\0'; o++)
        {
            if (*o == 'a')
                all = 1;
            else if (*o == 'l')
                long_format = 1;
            else if (*o != '1')
            {
                fprintf(stderr, "ls: unknown option -%c\n", *o);

                return 2;
            }
        }
    }

    if (i == argc)
        return list_path(".", all, long_format, 0);

    for (int operands = argc - i; i < argc; i++)
        status |= list_path(argv[i], all, long_format, operands > 1);

    return status;
}

static int make_directory(const char *path, int parents)
{
    char buffer[512];

    if (parents)
    {
        snprintf(buffer, sizeof(buffer), "%s", path);

        for (char *p = buffer + 1; *p != '\0'; p++)
        {
            if (*p != '/')
                continue;

            *p = '\0';

            if (mkdir(buffer, 0755) != 0 && errno != EEXIST)
                return -1;

            *p = '/';
        }

        return mkdir(buffer, 0755) != 0 && errno != EEXIST ? -1 : 0;
    }

    return mkdir(path, 0755);
}

static int box_mkdir(int argc, char **argv)
{
    int parents = argc > 1 && strcmp(argv[1], "-p") == 0;
    int status = 0;

    if (argc < 2 + parents)
    {
        fprintf(stderr, "usage: mkdir [-p] directory...\n");

        return 2;
    }

    for (int i = 1 + parents; i < argc; i++)
    {
        if (make_directory(argv[i], parents) != 0)
            status = fail(argv[i]);
    }

    return status;
}

static int box_rmdir(int argc, char **argv)
{
    int status = 0;

    if (argc < 2)
    {
        fprintf(stderr, "usage: rmdir directory...\n");

        return 2;
    }

    for (int i = 1; i < argc; i++)
    {
        if (rmdir(argv[i]) != 0)
            status = fail(argv[i]);
    }

    return status;
}

static int remove_tree(const char *path, int recursive, int force)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return force && errno == ENOENT ? 0 : fail(path);

    if (!S_ISDIR(st.st_mode))
        return unlink(path) == 0 ? 0 : fail(path);

    if (!recursive)
    {
        fprintf(stderr, "rm: %s: is a directory (use -r)\n", path);

        return 1;
    }

    DIR *dir = opendir(path);

    if (dir == NULL)
        return fail(path);

    /* Collect the names first: removing while reading would disturb the listing. */
    char **names = NULL;
    size_t count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char **grown = realloc(names, (count + 1) * sizeof(*names));

        if (grown == NULL)
            break;

        names = grown;
        names[count] = strdup(entry->d_name);

        if (names[count] == NULL)
            break;

        count++;
    }

    closedir(dir);

    int status = 0;

    for (size_t i = 0; i < count; i++)
    {
        char child[1024];

        snprintf(child, sizeof(child), "%s/%s", path, names[i]);
        status |= remove_tree(child, recursive, force);
        free(names[i]);
    }

    free(names);

    return status | (rmdir(path) == 0 ? 0 : fail(path));
}

static int box_rm(int argc, char **argv)
{
    int recursive = 0;
    int force = 0;
    int i = 1;
    int status = 0;

    for (; i < argc && is_option(argv[i]); i++)
    {
        for (const char *o = argv[i] + 1; *o != '\0'; o++)
        {
            if (*o == 'r' || *o == 'R')
                recursive = 1;
            else if (*o == 'f')
                force = 1;
            else
            {
                fprintf(stderr, "rm: unknown option -%c\n", *o);

                return 2;
            }
        }
    }

    if (i == argc)
    {
        fprintf(stderr, "usage: rm [-rf] file...\n");

        return 2;
    }

    for (; i < argc; i++)
        status |= remove_tree(argv[i], recursive, force);

    return status;
}

/* The path of the copy: into dst if that is a directory, else dst itself. */
static void target_path(char *out, size_t size, const char *src, const char *dst)
{
    struct stat st;

    if (stat(dst, &st) == 0 && S_ISDIR(st.st_mode))
        snprintf(out, size, "%s/%s", dst, base_name(src));
    else
        snprintf(out, size, "%s", dst);
}

static int box_cp(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: cp source destination\n");

        return 2;
    }

    char target[1024];

    target_path(target, sizeof(target), argv[1], argv[2]);

    int in = open(argv[1], O_RDONLY);

    if (in < 0)
        return fail(argv[1]);

    int out = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (out < 0)
    {
        int status = fail(target);

        close(in);

        return status;
    }

    int status = copy_fd(in, out) == 0 ? 0 : fail(target);

    close(in);
    close(out);

    return status;
}

static int box_mv(int argc, char **argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: mv source destination\n");

        return 2;
    }

    char target[1024];

    target_path(target, sizeof(target), argv[1], argv[2]);

    return rename(argv[1], target) == 0 ? 0 : fail(argv[1]);
}

static int box_touch(int argc, char **argv)
{
    int status = 0;

    for (int i = 1; i < argc; i++)
    {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);

        if (fd < 0)
            status = fail(argv[i]);
        else
            close(fd);
    }

    return status;
}

/* Run fn(fd, name) for each file operand, or for stdin. */
static int each_input(int argc, char **argv, int first, int (*fn)(int fd, const char *name))
{
    int status = 0;

    if (first >= argc)
        return fn(0, "-");

    for (int i = first; i < argc; i++)
    {
        int fd = strcmp(argv[i], "-") == 0 ? 0 : open(argv[i], O_RDONLY);

        if (fd < 0)
        {
            status = fail(argv[i]);
            continue;
        }

        status |= fn(fd, argv[i]);

        if (fd != 0)
            close(fd);
    }

    return status;
}

static int wc_lines, wc_words, wc_bytes; /* which counts to print */

static int wc_one(int fd, const char *name)
{
    char buffer[4096];
    unsigned long lines = 0, words = 0, bytes = 0;
    int in_word = 0;

    for (;;)
    {
        ssize_t n = read(fd, buffer, sizeof(buffer));

        if (n < 0)
            return fail(name);

        if (n == 0)
            break;

        bytes += (unsigned long)n;

        for (ssize_t i = 0; i < n; i++)
        {
            if (buffer[i] == '\n')
                lines++;

            if (isspace((unsigned char)buffer[i]))
                in_word = 0;
            else if (!in_word)
            {
                in_word = 1;
                words++;
            }
        }
    }

    if (wc_lines)
        printf("%7lu", lines);

    if (wc_words)
        printf("%7lu", words);

    if (wc_bytes)
        printf("%7lu", bytes);

    printf(" %s\n", strcmp(name, "-") == 0 ? "" : name);

    return 0;
}

static int box_wc(int argc, char **argv)
{
    int i = 1;

    wc_lines = wc_words = wc_bytes = 0;

    for (; i < argc && is_option(argv[i]); i++)
    {
        for (const char *o = argv[i] + 1; *o != '\0'; o++)
        {
            if (*o == 'l')
                wc_lines = 1;
            else if (*o == 'w')
                wc_words = 1;
            else if (*o == 'c')
                wc_bytes = 1;
            else
            {
                fprintf(stderr, "wc: unknown option -%c\n", *o);

                return 2;
            }
        }
    }

    if (!wc_lines && !wc_words && !wc_bytes)
        wc_lines = wc_words = wc_bytes = 1;

    return each_input(argc, argv, i, wc_one);
}

static long line_count = 10;

/* "-n N" or "-nN": returns the index of the first operand, or -1 on a usage error. */
static int parse_count(int argc, char **argv)
{
    int i = 1;

    line_count = 10;

    while (i < argc && is_option(argv[i]))
    {
        const char *value;

        if (strncmp(argv[i], "-n", 2) != 0)
            return -1;

        if (argv[i][2] != '\0')
            value = argv[i] + 2;
        else if (i + 1 < argc)
            value = argv[++i];
        else
            return -1;

        char *end;

        line_count = strtol(value, &end, 10);

        if (*end != '\0' || line_count < 0)
            return -1;

        i++;
    }

    return i;
}

static int head_one(int fd, const char *name)
{
    char buffer[4096];
    long lines = 0;

    while (lines < line_count)
    {
        ssize_t n = read(fd, buffer, sizeof(buffer));

        if (n < 0)
            return fail(name);

        if (n == 0)
            break;

        ssize_t used = 0;

        while (used < n && lines < line_count)
        {
            if (buffer[used++] == '\n')
                lines++;
        }

        fwrite(buffer, 1, (size_t)used, stdout);
    }

    return 0;
}

static int box_head(int argc, char **argv)
{
    int first = parse_count(argc, argv);

    if (first < 0)
    {
        fprintf(stderr, "usage: head [-n lines] [file...]\n");

        return 2;
    }

    return each_input(argc, argv, first, head_one);
}

/* Read a whole input into memory. Returns the length, or -1 (errno set). */
static ssize_t slurp(int fd, char **data)
{
    size_t size = 0;
    size_t capacity = 4096;
    char *buffer = malloc(capacity);

    if (buffer == NULL)
        return -1;

    for (;;)
    {
        if (size == capacity)
        {
            char *grown = realloc(buffer, capacity * 2);

            if (grown == NULL)
            {
                free(buffer);

                return -1;
            }

            buffer = grown;
            capacity *= 2;
        }

        ssize_t n = read(fd, buffer + size, capacity - size);

        if (n < 0)
        {
            free(buffer);

            return -1;
        }

        if (n == 0)
            break;

        size += (size_t)n;
    }

    *data = buffer;

    return (ssize_t)size;
}

static int tail_one(int fd, const char *name)
{
    char *data;
    ssize_t size = slurp(fd, &data);

    if (size < 0)
        return fail(name);

    /* Walk back over line_count lines (a final newline ends the last line). */
    ssize_t start = size;
    long lines = 0;

    while (start > 0 && lines < line_count)
    {
        start--;

        if (data[start] == '\n' && start != size - 1)
            lines++;
    }

    if (lines == line_count && start < size && data[start] == '\n')
        start++;

    fwrite(data + start, 1, (size_t)(size - start), stdout);
    free(data);

    return 0;
}

static int box_tail(int argc, char **argv)
{
    int first = parse_count(argc, argv);

    if (first < 0)
    {
        fprintf(stderr, "usage: tail [-n lines] [file...]\n");

        return 2;
    }

    return each_input(argc, argv, first, tail_one);
}

static int grep_ignore_case, grep_invert, grep_number, grep_count_only;
static const char *grep_pattern;
static int grep_show_name;

static int grep_one(int fd, const char *name)
{
    char *data;
    ssize_t size = slurp(fd, &data);

    if (size < 0)
        return fail(name);

    long matches = 0;
    long number = 0;
    ssize_t pos = 0;

    while (pos < size)
    {
        ssize_t end = pos;

        while (end < size && data[end] != '\n')
            end++;

        char *line = strndup(data + pos, (size_t)(end - pos));

        if (line == NULL)
            break;

        number++;

        int found = (grep_ignore_case ? strcasestr(line, grep_pattern) : strstr(line, grep_pattern)) != NULL;

        if (found != grep_invert)
        {
            matches++;

            if (!grep_count_only)
            {
                if (grep_show_name)
                    printf("%s:", name);

                if (grep_number)
                    printf("%ld:", number);

                printf("%s\n", line);
            }
        }

        free(line);
        pos = end + 1;
    }

    free(data);

    if (grep_count_only)
        printf("%ld\n", matches);

    return matches > 0 ? 0 : 1;
}

static int box_grep(int argc, char **argv)
{
    int i = 1;

    grep_ignore_case = grep_invert = grep_number = grep_count_only = 0;

    for (; i < argc && is_option(argv[i]); i++)
    {
        for (const char *o = argv[i] + 1; *o != '\0'; o++)
        {
            if (*o == 'i')
                grep_ignore_case = 1;
            else if (*o == 'v')
                grep_invert = 1;
            else if (*o == 'n')
                grep_number = 1;
            else if (*o == 'c')
                grep_count_only = 1;
            else
            {
                fprintf(stderr, "grep: unknown option -%c\n", *o);

                return 2;
            }
        }
    }

    if (i >= argc)
    {
        fprintf(stderr, "usage: grep [-ivnc] text [file...]\n");

        return 2;
    }

    grep_pattern = argv[i++];
    grep_show_name = argc - i > 1;

    /* Exit status: 0 if some line matched in some file, 1 if none, 2 on an error. */
    int status = 1;
    int errors = 0;

    if (i >= argc)
        return grep_one(0, "-");

    for (; i < argc; i++)
    {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0)
        {
            fail(argv[i]);
            errors = 1;
            continue;
        }

        if (grep_one(fd, argv[i]) == 0)
            status = 0;

        close(fd);
    }

    return errors ? 2 : status;
}

static int sort_reverse, sort_numeric;

static int compare_lines(const void *a, const void *b)
{
    const char *x = *(char *const *)a;
    const char *y = *(char *const *)b;
    int result;

    if (sort_numeric)
    {
        double dx = strtod(x, NULL);
        double dy = strtod(y, NULL);

        result = dx < dy ? -1 : dx > dy;
    }
    else
    {
        result = strcmp(x, y);
    }

    return sort_reverse ? -result : result;
}

static int box_sort(int argc, char **argv)
{
    int i = 1;

    sort_reverse = sort_numeric = 0;

    for (; i < argc && is_option(argv[i]); i++)
    {
        for (const char *o = argv[i] + 1; *o != '\0'; o++)
        {
            if (*o == 'r')
                sort_reverse = 1;
            else if (*o == 'n')
                sort_numeric = 1;
            else
            {
                fprintf(stderr, "sort: unknown option -%c\n", *o);

                return 2;
            }
        }
    }

    char **lines = NULL;
    size_t count = 0;
    char *line = NULL;
    size_t capacity = 0;
    int status = 0;

    for (int operand = i; operand < argc || (operand == i && i >= argc); operand++)
    {
        FILE *f = i >= argc || strcmp(argv[operand], "-") == 0 ? stdin : fopen(argv[operand], "r");

        if (f == NULL)
        {
            status = fail(argv[operand]);
            continue;
        }

        ssize_t length;

        while ((length = getline(&line, &capacity, f)) >= 0)
        {
            if (length > 0 && line[length - 1] == '\n')
                line[length - 1] = '\0';

            char **grown = realloc(lines, (count + 1) * sizeof(*lines));

            if (grown == NULL)
                return fail("memory");

            lines = grown;
            lines[count++] = strdup(line);
        }

        if (f != stdin)
            fclose(f);

        if (i >= argc)
            break;
    }

    qsort(lines, count, sizeof(*lines), compare_lines);

    for (size_t n = 0; n < count; n++)
    {
        puts(lines[n]);
        free(lines[n]);
    }

    free(lines);
    free(line);

    return status;
}

static int uniq_one(int fd, const char *name)
{
    char *data;
    ssize_t size = slurp(fd, &data);

    if (size < 0)
        return fail(name);

    ssize_t pos = 0;
    char *previous = NULL;

    while (pos < size)
    {
        ssize_t end = pos;

        while (end < size && data[end] != '\n')
            end++;

        char *line = strndup(data + pos, (size_t)(end - pos));

        if (line != NULL && (previous == NULL || strcmp(line, previous) != 0))
            printf("%s\n", line);

        free(previous);
        previous = line;
        pos = end + 1;
    }

    free(previous);
    free(data);

    return 0;
}

static int box_uniq(int argc, char **argv)
{
    return each_input(argc, argv, 1, uniq_one);
}

static int hexdump_one(int fd, const char *name)
{
    unsigned char buffer[16];
    unsigned long offset = 0;

    for (;;)
    {
        ssize_t n = 0;

        /* A full line of 16 bytes even when read() returns less. */
        while (n < 16)
        {
            ssize_t got = read(fd, buffer + n, (size_t)(16 - n));

            if (got < 0)
                return fail(name);

            if (got == 0)
                break;

            n += got;
        }

        if (n == 0)
            break;

        printf("%08lx ", offset);

        for (int i = 0; i < 16; i++)
        {
            if (i == 8)
                printf(" ");

            if (i < n)
                printf(" %02x", buffer[i]);
            else
                printf("   ");
        }

        printf("  |");

        for (int i = 0; i < n; i++)
            putchar(isprint(buffer[i]) ? buffer[i] : '.');

        printf("|\n");
        offset += (unsigned long)n;

        if (n < 16)
            break;
    }

    printf("%08lx\n", offset);

    return 0;
}

static int box_hexdump(int argc, char **argv)
{
    int first = 1;

    /* -C is the only format; accept it for habit. */
    if (argc > 1 && strcmp(argv[1], "-C") == 0)
        first = 2;

    return each_input(argc, argv, first, hexdump_one);
}

static int box_seq(int argc, char **argv)
{
    long first = 1;
    long last;

    if (argc == 2)
        last = strtol(argv[1], NULL, 10);
    else if (argc == 3)
    {
        first = strtol(argv[1], NULL, 10);
        last = strtol(argv[2], NULL, 10);
    }
    else
    {
        fprintf(stderr, "usage: seq [first] last\n");

        return 2;
    }

    for (long n = first; n <= last; n++)
        printf("%ld\n", n);

    return 0;
}

static int box_sleep(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: sleep seconds\n");

        return 2;
    }

    double seconds = strtod(argv[1], NULL);
    struct timespec delay = {(time_t)seconds, (long)((seconds - (double)(time_t)seconds) * 1e9)};

    nanosleep(&delay, NULL);

    return 0;
}

static int box_uname(int argc, char **argv)
{
    struct utsname name;

    if (uname(&name) != 0)
        return fail("uname");

    if (argc > 1 && strcmp(argv[1], "-a") == 0)
        printf("%s %s %s %s %s\n", name.sysname, name.nodename, name.release, name.version, name.machine);
    else
        printf("%s\n", name.sysname);

    return 0;
}

static int box_date(int argc, char **argv)
{
    time_t now = time(NULL);
    char text[64];

    (void)argc;
    (void)argv;

    /* There are no time zones: the clock is UTC. */
    strftime(text, sizeof(text), "%a %b %e %H:%M:%S UTC %Y", gmtime(&now));
    printf("%s\n", text);

    return 0;
}

static int box_env(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    for (char **e = environ; *e != NULL; e++)
        printf("%s\n", *e);

    return 0;
}

static int box_basename(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: basename path\n");

        return 2;
    }

    char *path = strdup(argv[1]);
    size_t length = strlen(path);

    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';

    printf("%s\n", base_name(path));
    free(path);

    return 0;
}

static int box_dirname(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: dirname path\n");

        return 2;
    }

    char *path = strdup(argv[1]);
    size_t length = strlen(path);

    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';

    char *slash = strrchr(path, '/');

    if (slash == NULL)
        printf(".\n");
    else if (slash == path)
        printf("/\n");
    else
    {
        *slash = '\0';
        printf("%s\n", path);
    }

    free(path);

    return 0;
}

static int box_tee(int argc, char **argv)
{
    int append = argc > 1 && strcmp(argv[1], "-a") == 0;
    int outputs[8];
    int count = 0;

    for (int i = 1 + append; i < argc && count < 8; i++)
    {
        int fd = open(argv[i], O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC), 0644);

        if (fd < 0)
            fail(argv[i]);
        else
            outputs[count++] = fd;
    }

    char buffer[1024];
    ssize_t n;

    while ((n = read(0, buffer, sizeof(buffer))) > 0)
    {
        if (write(1, buffer, (size_t)n) < 0)
            break;

        for (int i = 0; i < count; i++)
        {
            if (write(outputs[i], buffer, (size_t)n) < 0)
                break;
        }
    }

    for (int i = 0; i < count; i++)
        close(outputs[i]);

    return 0;
}

static int box_stat(int argc, char **argv)
{
    int status = 0;

    for (int i = 1; i < argc; i++)
    {
        struct stat st;

        if (stat(argv[i], &st) != 0)
        {
            status = fail(argv[i]);
            continue;
        }

        printf("%s: %s, %lld bytes\n", argv[i],
               S_ISDIR(st.st_mode) ? "directory" : S_ISCHR(st.st_mode) ? "character device" : "file",
               (long long)st.st_size);
    }

    return status;
}

/* The number after "name:" in /proc/meminfo, in kB; -1 if it is not there. */
static long meminfo_value(const char *text, const char *name)
{
    const char *line = strstr(text, name);

    if (line == NULL)
        return -1;

    return strtol(line + strlen(name), NULL, 10);
}

static int box_free(int argc, char **argv)
{
    char text[1024];
    int fd = open("/proc/meminfo", O_RDONLY);

    (void)argc;
    (void)argv;

    if (fd < 0)
        return fail("/proc/meminfo");

    ssize_t n = read(fd, text, sizeof(text) - 1);

    close(fd);

    if (n < 0)
        return fail("/proc/meminfo");

    text[n] = '\0';

    long total = meminfo_value(text, "MemTotal:");
    long available = meminfo_value(text, "MemFree:");

    printf("%12s%12s%12s\n", "total", "used", "free");
    printf("Mem:%8ld%12ld%12ld\n", total, total - available, available);

    return 0;
}

static int box_uptime(int argc, char **argv)
{
    char text[64];
    int fd = open("/proc/uptime", O_RDONLY);

    (void)argc;
    (void)argv;

    if (fd < 0)
        return fail("/proc/uptime");

    ssize_t n = read(fd, text, sizeof(text) - 1);

    close(fd);

    if (n < 0)
        return fail("/proc/uptime");

    text[n] = '\0';

    long seconds = strtol(text, NULL, 10);

    printf("up %ld:%02ld:%02ld\n", seconds / 3600, seconds / 60 % 60, seconds % 60);

    return 0;
}

/* ps: the process list of the kernel (/proc/processes). */
static int box_ps(int argc, char **argv)
{
    char text[4096];
    int fd = open("/proc/processes", O_RDONLY);

    (void)argc;
    (void)argv;

    if (fd < 0)
        return fail("/proc/processes");

    ssize_t n = read(fd, text, sizeof(text) - 1);

    close(fd);

    if (n < 0)
        return fail("/proc/processes");

    fwrite(text, 1, (size_t)n, stdout);

    return 0;
}

/* kill [-SIGNAL-NUMBER] pid... */
static int box_kill(int argc, char **argv)
{
    int signal_number = 15;
    int first = 1;
    int status = 0;

    if (argc > 1 && argv[1][0] == '-')
    {
        signal_number = atoi(argv[1] + 1);
        first = 2;
    }

    if (first >= argc)
    {
        fprintf(stderr, "usage: kill [-signal] pid...\n");

        return 1;
    }

    for (int i = first; i < argc; i++)
    {
        if (kill((pid_t)atoi(argv[i]), signal_number) != 0)
        {
            perror(argv[i]);
            status = 1;
        }
    }

    return status;
}

static int box_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\x1b[2J\x1b[H");

    return 0;
}

static const struct
{
    const char *name;
    applet_fn function;
} applets[] = {
    {"basename", box_basename}, {"cat", box_cat},     {"clear", box_clear},   {"cp", box_cp},
    {"date", box_date},         {"dirname", box_dirname}, {"env", box_env},   {"false", box_false},
    {"grep", box_grep},         {"head", box_head},   {"hexdump", box_hexdump}, {"ls", box_ls},
    {"mkdir", box_mkdir},       {"mv", box_mv},       {"pwd", box_pwd},       {"rm", box_rm},
    {"rmdir", box_rmdir},       {"seq", box_seq},     {"sleep", box_sleep},   {"sort", box_sort},
    {"stat", box_stat},         {"tail", box_tail},   {"tee", box_tee},       {"touch", box_touch},
    {"true", box_true},         {"uname", box_uname}, {"uniq", box_uniq},     {"wc", box_wc},
    {"echo", box_echo},         {"free", box_free},   {"uptime", box_uptime},
    {"ps", box_ps},             {"kill", box_kill},
};

#define APPLET_COUNT ((int)(sizeof(applets) / sizeof(applets[0])))

int main(int argc, char **argv)
{
    const char *name = base_name(argv[0]);

    /* Started as "box": the first argument is the applet. */
    if (strcmp(name, "box") == 0)
    {
        if (argc < 2)
        {
            printf("box applets:");

            for (int i = 0; i < APPLET_COUNT; i++)
                printf(" %s", applets[i].name);

            printf("\n");

            return 0;
        }

        if (strcmp(argv[1], "--list") == 0)
        {
            for (int i = 0; i < APPLET_COUNT; i++)
                printf("%s\n", applets[i].name);

            return 0;
        }

        argv++;
        argc--;
        name = base_name(argv[0]);
    }

    program = name;

    for (int i = 0; i < APPLET_COUNT; i++)
    {
        if (strcmp(name, applets[i].name) == 0)
            return applets[i].function(argc, argv);
    }

    return 127;
}

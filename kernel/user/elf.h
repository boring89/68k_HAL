/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_USER_ELF_H
#define KERNEL_USER_ELF_H

#include <stddef.h>
#include <stdint.h>

/*
 * Reading statically linked 32-bit x86 ELF executables (System V ABI,
 * Intel386 supplement; the "ELF-32" structures of the Tool Interface
 * Standard ELF 1.2). Pure parsing of a file image in memory.
 */

#define ELF_MAX_SEGMENTS 8

#define ELF_PF_X 1u
#define ELF_PF_W 2u
#define ELF_PF_R 4u

typedef struct
{
    uint32_t vaddr;  /* where the segment starts in the program */
    uint32_t offset; /* where its bytes start in the file */
    uint32_t filesz; /* bytes to copy from the file */
    uint32_t memsz;  /* bytes the segment occupies; the rest (>= filesz) is zeroed */
    uint32_t flags;  /* ELF_PF_* */
} elf_segment_t;

typedef struct
{
    uint32_t entry;
    uint32_t phdr_vaddr; /* where the program headers are in memory, 0 if unknown */
    uint32_t phent;      /* size of one program header */
    uint32_t phnum;
    unsigned int segment_count;
    elf_segment_t segments[ELF_MAX_SEGMENTS];
} elf_image_t;

/*
 * Check that data (size bytes) is an ELF32 little-endian i386 ET_EXEC
 * file and collect its PT_LOAD segments, each of which must lie inside
 * the file and satisfy filesz <= memsz. Returns 0, or -1 with the
 * reason in *error (a static string) if the file is not acceptable.
 */
int elf_parse(const void *data, size_t size, elf_image_t *image,
              const char **error);

#endif

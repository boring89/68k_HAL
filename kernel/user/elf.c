/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "elf.h"

/*
 * ELF header and program header field offsets (TIS ELF 1.2, Book I,
 * 1-3 and 2-2; "Elf32_Ehdr" and "Elf32_Phdr").
 */
#define EHDR_SIZE 52
#define PHDR_SIZE 32

#define ET_EXEC 2
#define EM_386 3
#define PT_LOAD 1
#define PT_PHDR 6

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t rd32(const uint8_t *p)
{
    return rd16(p) | rd16(p + 2) << 16;
}

static int fail(const char **error, const char *message)
{
    if (error != NULL)
        *error = message;

    return -1;
}

int elf_parse(const void *data, size_t size, elf_image_t *image,
              const char **error)
{
    const uint8_t *file = data;

    if (size < EHDR_SIZE)
        return fail(error, "file too small");

    /* e_ident: magic, EI_CLASS = 1 (32-bit), EI_DATA = 1 (little endian). */
    if (file[0] != 0x7F || file[1] != 'E' || file[2] != 'L' || file[3] != 'F')
        return fail(error, "not an ELF file");

    if (file[4] != 1 || file[5] != 1)
        return fail(error, "not a 32-bit little-endian ELF file");

    if (rd16(file + 16) != ET_EXEC)
        return fail(error, "not a static executable (ET_EXEC)");

    if (rd16(file + 18) != EM_386)
        return fail(error, "not an i386 program");

    uint32_t phoff = rd32(file + 28);
    uint32_t phentsize = rd16(file + 42);
    uint32_t phnum = rd16(file + 44);

    if (phentsize < PHDR_SIZE)
        return fail(error, "bad program header size");

    if (phoff > size || phnum > (size - phoff) / phentsize)
        return fail(error, "program headers outside the file");

    image->entry = rd32(file + 24);
    image->phdr_vaddr = 0;
    image->phent = phentsize;
    image->phnum = phnum;
    image->segment_count = 0;

    for (uint32_t i = 0; i < phnum; i++)
    {
        const uint8_t *ph = file + phoff + (size_t)i * phentsize;
        uint32_t type = rd32(ph);

        if (type == PT_PHDR)
            image->phdr_vaddr = rd32(ph + 8);

        if (type != PT_LOAD)
            continue;

        elf_segment_t seg;

        seg.offset = rd32(ph + 4);
        seg.vaddr = rd32(ph + 8);
        seg.filesz = rd32(ph + 16);
        seg.memsz = rd32(ph + 20);
        seg.flags = rd32(ph + 24);

        if (seg.memsz == 0)
            continue;

        if (seg.filesz > seg.memsz)
            return fail(error, "segment file size exceeds memory size");

        if (seg.offset > size || seg.filesz > size - seg.offset)
            return fail(error, "segment outside the file");

        if (seg.vaddr + seg.memsz < seg.vaddr)
            return fail(error, "segment wraps around the address space");

        if (image->segment_count == ELF_MAX_SEGMENTS)
            return fail(error, "too many segments");

        image->segments[image->segment_count++] = seg;
    }

    if (image->segment_count == 0)
        return fail(error, "no loadable segments");

    /* Without PT_PHDR (static, non-PIE) the headers sit in the first segment. */
    if (image->phdr_vaddr == 0)
    {
        const elf_segment_t *first = &image->segments[0];

        if (phoff >= first->offset && phoff < first->offset + first->filesz)
            image->phdr_vaddr = first->vaddr + (phoff - first->offset);
    }

    return 0;
}

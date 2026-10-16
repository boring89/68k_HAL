/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/user/elf.h"

#include <string.h>

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

/* A minimal ET_EXEC with one text and one data segment. */
static size_t make_elf(uint8_t *f)
{
    memset(f, 0, 512);
    memcpy(f, "\x7f" "ELF", 4);
    f[4] = 1;
    f[5] = 1;
    f[6] = 1;
    put16(f + 16, 2);
    put16(f + 18, 3);
    put32(f + 20, 1);
    put32(f + 24, 0x08049000);
    put32(f + 28, 52);
    put16(f + 40, 52);
    put16(f + 42, 32);
    put16(f + 44, 2);

    uint8_t *ph = f + 52;

    put32(ph, 1);              /* PT_LOAD */
    put32(ph + 4, 0);          /* offset */
    put32(ph + 8, 0x08048000); /* vaddr */
    put32(ph + 16, 0x100);     /* filesz */
    put32(ph + 20, 0x100);     /* memsz */
    put32(ph + 24, 5);         /* R+X */

    ph += 32;
    put32(ph, 1);
    put32(ph + 4, 0x100);
    put32(ph + 8, 0x0804A000);
    put32(ph + 16, 0x80);
    put32(ph + 20, 0x2000); /* bss */
    put32(ph + 24, 6);      /* R+W */

    return 512;
}

static void test_valid(void)
{
    uint8_t f[512];
    elf_image_t image;
    const char *error = NULL;

    TEST_ASSERT_EQ_INT(0, elf_parse(f, make_elf(f), &image, &error));
    TEST_ASSERT_EQ_INT(0x08049000, image.entry);
    TEST_ASSERT_EQ_INT(2, image.segment_count);
    TEST_ASSERT_EQ_INT(0x0804A000, image.segments[1].vaddr);
    TEST_ASSERT_EQ_INT(0x2000, image.segments[1].memsz);
    TEST_ASSERT_EQ_INT(ELF_PF_R | ELF_PF_W, image.segments[1].flags);
    /* The headers (offset 52) are inside the first segment. */
    TEST_ASSERT_EQ_INT(0x08048000 + 52, image.phdr_vaddr);
    TEST_ASSERT_EQ_INT(2, image.phnum);
}

static void test_rejects(void)
{
    uint8_t f[512];
    elf_image_t image;
    const char *error = NULL;
    size_t size = make_elf(f);

    TEST_ASSERT_EQ_INT(-1, elf_parse(f, 20, &image, &error));

    f[0] = 'X';
    TEST_ASSERT_EQ_INT(-1, elf_parse(f, size, &image, &error));
    make_elf(f);

    f[4] = 2; /* 64-bit */
    TEST_ASSERT_EQ_INT(-1, elf_parse(f, size, &image, &error));
    make_elf(f);

    put16(f + 16, 3); /* ET_DYN */
    TEST_ASSERT_EQ_INT(-1, elf_parse(f, size, &image, &error));
    make_elf(f);

    put16(f + 18, 62); /* x86-64 */
    TEST_ASSERT_EQ_INT(-1, elf_parse(f, size, &image, &error));
    make_elf(f);

    put32(f + 52 + 16, 0x1000); /* segment bigger than the file */
    put32(f + 52 + 20, 0x1000);
    TEST_ASSERT_EQ_INT(-1, elf_parse(f, size, &image, &error));
    make_elf(f);

    put32(f + 52 + 16, 0x200); /* filesz > memsz */
    TEST_ASSERT_EQ_INT(-1, elf_parse(f, size, &image, &error));
    make_elf(f);

    put32(f + 28, 0x10000); /* program headers outside the file */
    TEST_ASSERT_EQ_INT(-1, elf_parse(f, size, &image, &error));
    make_elf(f);

    put32(f + 52 + 8, 0xFFFFFF00u); /* wraps around */
    put32(f + 52 + 20, 0x200);
    TEST_ASSERT_EQ_INT(-1, elf_parse(f, size, &image, &error));
    TEST_ASSERT(error != NULL);
}

int main(void)
{
    TEST_RUN(test_valid);
    TEST_RUN(test_rejects);

    return TEST_REPORT("elf");
}

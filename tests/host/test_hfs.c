/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdint.h>
#include <stdlib.h>

#include "kernel/block/block.h"
#include "kernel/block/ramdisk.h"
#include "kernel/fs/hfs/hfs.h"

/* Built by tools/make_hfs_image.sh (see the Makefile); contents are listed there. */
#define IMAGE_PATH "build/images/hfs.img"
#define IMAGE_SIZE (800 * 1024)

static uint8_t image[IMAGE_SIZE];
static hfs_volume_t volume;

static uint8_t pattern_byte(uint32_t i, uint32_t seed)
{
    return (uint8_t)(i * 7 + i / 251 + seed);
}

static bool setup(void)
{
    FILE *f = fopen(IMAGE_PATH, "rb");

    block_reset();
    ramdisk_reset();

    if (f == NULL)
    {
        fprintf(stderr, "  cannot open %s\n", IMAGE_PATH);
        test_failures++;
        return false;
    }

    size_t got = fread(image, 1, sizeof(image), f);

    fclose(f);
    TEST_ASSERT_EQ_INT(IMAGE_SIZE, got);
    TEST_ASSERT_EQ_INT(0, ramdisk_register("hfs", image, sizeof(image)));
    TEST_ASSERT_EQ_INT(0, hfs_mount(&volume, block_device_find("hfs"), 0, 0));

    return true;
}

static void test_mount(void)
{
    if (!setup())
        return;

    TEST_ASSERT_EQ_INT(0, strcmp(volume.name, "TESTVOL"));
    TEST_ASSERT_EQ_INT(512, volume.alloc_block_size);
    TEST_ASSERT(volume.alloc_block_count > 1000);
    TEST_ASSERT(volume.free_alloc_blocks > 0);
    /* Root: Big, Docs, Empty, Fill, Frag, ReadMe, WithRsrc; plus the others. */
    TEST_ASSERT(volume.file_count > 500);
    TEST_ASSERT(volume.folder_count == 4);
}

static void test_mount_rejects(void)
{
    static uint8_t zeros[8192];

    block_reset();
    ramdisk_reset();
    TEST_ASSERT_EQ_INT(0, ramdisk_register("z", zeros, sizeof(zeros)));

    block_device_t *dev = block_device_find("z");

    TEST_ASSERT_EQ_INT(-1, hfs_mount(&volume, dev, 0, 0));
    TEST_ASSERT_EQ_INT(-1, hfs_mount(&volume, dev, 100, 0));   /* past the end */
    TEST_ASSERT_EQ_INT(-1, hfs_mount(NULL, dev, 0, 0));
    TEST_ASSERT_EQ_INT(-1, hfs_mount(&volume, NULL, 0, 0));

    /* A volume that claims more blocks than the device has. */
    if (!setup())
        return;

    block_reset();
    ramdisk_reset();
    TEST_ASSERT_EQ_INT(0, ramdisk_register("short", image, 100 * 512));
    TEST_ASSERT_EQ_INT(-1, hfs_mount(&volume, block_device_find("short"), 0, 0));

    /* A wrong signature. */
    image[2 * 512] = 'X';
    block_reset();
    ramdisk_reset();
    TEST_ASSERT_EQ_INT(0, ramdisk_register("bad", image, IMAGE_SIZE));
    TEST_ASSERT_EQ_INT(-1, hfs_mount(&volume, block_device_find("bad"), 0, 0));
}

static void expect_text(const char *path, const char *text, uint32_t type)
{
    hfs_entry_t entry;
    char buffer[64];

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, path, &entry));
    TEST_ASSERT(!entry.is_folder);
    TEST_ASSERT_EQ_INT(strlen(text), entry.data_length);
    TEST_ASSERT_EQ_INT(type, entry.file_type);
    TEST_ASSERT_EQ_INT(strlen(text),
                       hfs_read(&volume, &entry, HFS_FORK_DATA, 0, buffer,
                                sizeof(buffer)));
    TEST_ASSERT(memcmp(buffer, text, strlen(text)) == 0);
}

#define TYPE_TEXT 0x54455854u /* 'TEXT' */

static void test_lookup_files(void)
{
    if (!setup())
        return;

    expect_text("ReadMe", "Hello from HFS\r", TYPE_TEXT);
    expect_text(":ReadMe", "Hello from HFS\r", TYPE_TEXT);
    expect_text("Docs:Note", "A note in Docs\r", TYPE_TEXT);
    expect_text("Docs:Deep:Deeper:Leaf", "Leaf of the deepest folder\r", TYPE_TEXT);
    expect_text(":Docs::Deep:Mixed Case Name", "Mixed case name\r", TYPE_TEXT);

    /* Names compare without regard to the case of ASCII letters. */
    expect_text("README", "Hello from HFS\r", TYPE_TEXT);
    expect_text("dOcS:dEeP:mixed CASE name", "Mixed case name\r", TYPE_TEXT);
}

static void test_lookup_folders_and_failures(void)
{
    hfs_entry_t entry;

    if (!setup())
        return;

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, "", &entry));
    TEST_ASSERT(entry.is_folder);
    TEST_ASSERT_EQ_INT(HFS_ROOT_ID, entry.id);
    TEST_ASSERT_EQ_INT(0, strcmp(entry.name, "TESTVOL"));

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, ":", &entry));
    TEST_ASSERT_EQ_INT(HFS_ROOT_ID, entry.id);

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, "Docs", &entry));
    TEST_ASSERT(entry.is_folder);
    TEST_ASSERT_EQ_INT(2, entry.valence);
    TEST_ASSERT_EQ_INT(HFS_ROOT_ID, entry.parent_id);

    TEST_ASSERT_EQ_INT(-1, hfs_lookup(&volume, "Nothing", &entry));
    TEST_ASSERT_EQ_INT(-1, hfs_lookup(&volume, "Docs:Nothing", &entry));
    /* A file is not a folder to look inside. */
    TEST_ASSERT_EQ_INT(-1, hfs_lookup(&volume, "ReadMe:Docs", &entry));
    /* The same name under another parent does not match: Note is in Docs. */
    TEST_ASSERT_EQ_INT(-1, hfs_lookup(&volume, "Note", &entry));
    /* Component longer than 31 characters. */
    TEST_ASSERT_EQ_INT(-1, hfs_lookup(&volume,
                                      "Docs:0123456789012345678901234567890123",
                                      &entry));
    TEST_ASSERT_EQ_INT(-1, hfs_lookup(NULL, "Docs", &entry));
    TEST_ASSERT_EQ_INT(-1, hfs_lookup(&volume, NULL, &entry));
    TEST_ASSERT_EQ_INT(-1, hfs_lookup(&volume, "Docs", NULL));
}

static void test_empty_file_and_read_bounds(void)
{
    hfs_entry_t entry;
    char buffer[32];

    if (!setup())
        return;

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, "Empty", &entry));
    TEST_ASSERT_EQ_INT(0, entry.data_length);
    TEST_ASSERT_EQ_INT(0, hfs_read(&volume, &entry, HFS_FORK_DATA, 0, buffer, 32));

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, "ReadMe", &entry));
    /* Reads are cut at the end of the fork; at or past it they return 0. */
    TEST_ASSERT_EQ_INT(5, hfs_read(&volume, &entry, HFS_FORK_DATA, 10, buffer, 32));
    TEST_ASSERT(memcmp(buffer, " HFS\r", 5) == 0);
    TEST_ASSERT_EQ_INT(0, hfs_read(&volume, &entry, HFS_FORK_DATA, 15, buffer, 32));
    TEST_ASSERT_EQ_INT(0, hfs_read(&volume, &entry, HFS_FORK_DATA, 99999, buffer, 32));
    /* This file has no resource fork. */
    TEST_ASSERT_EQ_INT(0, hfs_read(&volume, &entry, HFS_FORK_RESOURCE, 0, buffer, 32));

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, "Docs", &entry));
    TEST_ASSERT_EQ_INT(-1, hfs_read(&volume, &entry, HFS_FORK_DATA, 0, buffer, 32));
    TEST_ASSERT_EQ_INT(-1, hfs_read(&volume, NULL, HFS_FORK_DATA, 0, buffer, 32));
}

static void test_resource_fork(void)
{
    hfs_entry_t entry;
    uint8_t buffer[4000];

    if (!setup())
        return;

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, "WithRsrc", &entry));
    TEST_ASSERT_EQ_INT(TYPE_TEXT, entry.file_type);
    TEST_ASSERT_EQ_INT(0x74747874, entry.file_creator); /* 'ttxt' */
    TEST_ASSERT_EQ_INT(22, entry.data_length);
    TEST_ASSERT_EQ_INT(3000, entry.rsrc_length);

    TEST_ASSERT_EQ_INT(22, hfs_read(&volume, &entry, HFS_FORK_DATA, 0, buffer, 4000));
    TEST_ASSERT(memcmp(buffer, "Data fork of WithRsrc\r", 22) == 0);

    TEST_ASSERT_EQ_INT(3000, hfs_read(&volume, &entry, HFS_FORK_RESOURCE, 0, buffer, 4000));

    int bad = 0;

    for (uint32_t i = 0; i < 3000; i++)
        bad += buffer[i] != pattern_byte(i, 3);

    TEST_ASSERT_EQ_INT(0, bad);
}

/* Read a whole file in odd-sized pieces and compare with the pattern. */
static void check_pattern_file(const char *path, uint32_t size, uint32_t seed)
{
    hfs_entry_t entry;
    static uint8_t chunk[977];

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, path, &entry));
    TEST_ASSERT_EQ_INT(size, entry.data_length);

    uint32_t offset = 0;
    int bad = 0;

    while (offset < size)
    {
        int got = hfs_read(&volume, &entry, HFS_FORK_DATA, offset, chunk,
                           sizeof(chunk));

        TEST_ASSERT(got > 0);

        if (got <= 0)
            return;

        for (int i = 0; i < got; i++)
            bad += chunk[i] != pattern_byte(offset + (uint32_t)i, seed);

        offset += (uint32_t)got;
    }

    TEST_ASSERT_EQ_INT(0, bad);
    TEST_ASSERT_EQ_INT(size, offset);
}

static void test_big_file(void)
{
    if (!setup())
        return;

    check_pattern_file("Big", 100000, 1);
}

/* "Frag" is spread over 30 one-block extents: the catalog holds only 3. */
static void test_fragmented_file_uses_overflow_tree(void)
{
    hfs_entry_t entry;

    if (!setup())
        return;

    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, "Frag", &entry));

    unsigned int in_catalog = 0;

    for (int i = 0; i < 3; i++)
        in_catalog += entry.data_extents[i].count;

    TEST_ASSERT(in_catalog < 30);
    check_pattern_file("Frag", 30 * 512, 2);
}

typedef struct
{
    int count;
    int folders;
    int saw_docs;
    int stop_after;
} list_state_t;

static int count_entry(const hfs_entry_t *entry, void *context)
{
    list_state_t *state = context;

    state->count++;
    state->folders += entry->is_folder;
    state->saw_docs += strcmp(entry->name, "Docs") == 0;

    return state->stop_after != 0 && state->count == state->stop_after;
}

static void test_list(void)
{
    hfs_entry_t entry;
    list_state_t state = {0, 0, 0, 0};

    if (!setup())
        return;

    TEST_ASSERT_EQ_INT(0, hfs_list(&volume, HFS_ROOT_ID, count_entry, &state));
    TEST_ASSERT_EQ_INT(7, state.count);   /* Big Docs Empty Fill Frag ReadMe WithRsrc */
    TEST_ASSERT_EQ_INT(2, state.folders);
    TEST_ASSERT_EQ_INT(1, state.saw_docs);

    /* The fillers: every other one of 996 was deleted. */
    TEST_ASSERT_EQ_INT(0, hfs_lookup(&volume, "Fill", &entry));
    state = (list_state_t){0, 0, 0, 0};
    TEST_ASSERT_EQ_INT(0, hfs_list(&volume, entry.id, count_entry, &state));
    TEST_ASSERT_EQ_INT(entry.valence, state.count);
    TEST_ASSERT(state.count > 400);
    TEST_ASSERT_EQ_INT(0, state.folders);

    /* The callback can stop the walk. */
    state = (list_state_t){0, 0, 0, 3};
    TEST_ASSERT_EQ_INT(0, hfs_list(&volume, entry.id, count_entry, &state));
    TEST_ASSERT_EQ_INT(3, state.count);

    /* A folder with nothing in it, and a number nobody has. */
    state = (list_state_t){0, 0, 0, 0};
    TEST_ASSERT_EQ_INT(0, hfs_list(&volume, 99999, count_entry, &state));
    TEST_ASSERT_EQ_INT(0, state.count);

    TEST_ASSERT_EQ_INT(-1, hfs_list(&volume, HFS_ROOT_ID, NULL, &state));
}

static void test_names_equal(void)
{
    TEST_ASSERT(hfs_names_equal("abc", "ABC"));
    TEST_ASSERT(hfs_names_equal("", ""));
    TEST_ASSERT(!hfs_names_equal("abc", "abcd"));
    TEST_ASSERT(!hfs_names_equal("abcd", "abc"));
    TEST_ASSERT(!hfs_names_equal("a-b", "a_b"));
    /* Bytes above 0x7F are compared exactly. */
    TEST_ASSERT(hfs_names_equal("\x8a" "x", "\x8a" "X"));
    TEST_ASSERT(!hfs_names_equal("\x8a", "\x80"));
}

/* A damaged catalog must give an error, not a crash or an endless loop. */
static void test_damaged_catalog(void)
{
    hfs_entry_t entry;

    if (!setup())
        return;

    /* Point the first leaf's forward link back at itself. */
    uint32_t leaf = volume.catalog_first_leaf;
    uint64_t byte = (uint64_t)volume.alloc_start * 512 +
                    (uint64_t)volume.catalog_extents[0].start * 512 +
                    (uint64_t)leaf * 512;

    image[byte + 0] = (uint8_t)(leaf >> 24);
    image[byte + 1] = (uint8_t)(leaf >> 16);
    image[byte + 2] = (uint8_t)(leaf >> 8);
    image[byte + 3] = (uint8_t)leaf;

    /* An entry that does not exist forces a walk over every leaf. */
    TEST_ASSERT_EQ_INT(-1, hfs_lookup(&volume, "ZZZ-not-there", &entry));
    list_state_t state = {0, 0, 0, 0};

    TEST_ASSERT_EQ_INT(-1, hfs_list(&volume, 99999, count_entry, &state));
}

int main(void)
{
    TEST_RUN(test_mount);
    TEST_RUN(test_mount_rejects);
    TEST_RUN(test_lookup_files);
    TEST_RUN(test_lookup_folders_and_failures);
    TEST_RUN(test_empty_file_and_read_bounds);
    TEST_RUN(test_resource_fork);
    TEST_RUN(test_big_file);
    TEST_RUN(test_fragmented_file_uses_overflow_tree);
    TEST_RUN(test_list);
    TEST_RUN(test_names_equal);
    TEST_RUN(test_damaged_catalog);

    return TEST_REPORT("hfs");
}

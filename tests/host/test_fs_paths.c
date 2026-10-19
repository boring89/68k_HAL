/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "emu/mac/fs_paths.h"

/* A small host tree: path, folder?, contents. */
static const struct
{
    const char *path;
    bool dir;
    const char *data;
} tree[] = {
    {"readme.txt", false, "Hello\r"},
    {".hidden", false, "secret"},
    {"Docs", true, NULL},
    {"Docs/note.txt", false, "A note\r"},
    {"Docs/a:b", false, "colon"},
    {"Docs/caf\xC3\xA9.txt", false, "utf-8"},
    {"Docs/a-very-long-file-name-that-does-not-fit.txt", false, "long"},
    {"bin", true, NULL},
};

#define TREE (sizeof tree / sizeof tree[0])

static int parent_matches(const char *path, const char *dir)
{
    const char *slash = strrchr(path, '/');
    size_t n = slash == NULL ? 0 : (size_t)(slash - path);

    return strlen(dir) == n && strncmp(path, dir, n) == 0;
}

static int fake_entry(void *ctx, const char *path, uint32_t index, char *name, uint32_t size)
{
    uint32_t seen = 0;

    (void)ctx;
    for (size_t i = 0; i < TREE; i++)
    {
        if (!parent_matches(tree[i].path, path))
            continue;
        if (seen++ == index)
        {
            const char *slash = strrchr(tree[i].path, '/');

            snprintf(name, size, "%s", slash != NULL ? slash + 1 : tree[i].path);
            return 0;
        }
    }
    return -1;
}

static int find(const char *path)
{
    for (size_t i = 0; i < TREE; i++)
    {
        if (strcmp(tree[i].path, path) == 0)
            return (int)i;
    }
    return -1;
}

static int fake_stat(void *ctx, const char *path, bool *is_dir, uint32_t *size, uint32_t *mtime)
{
    (void)ctx;
    *mtime = 1000;
    if (path[0] == '\0')
    {
        *is_dir = true;
        *size = 0;
        return 0;
    }

    int i = find(path);

    if (i < 0)
        return -1;
    *is_dir = tree[i].dir;
    *size = tree[i].dir ? 0 : (uint32_t)strlen(tree[i].data);
    return 0;
}

static int32_t fake_read(void *ctx, const char *path, uint32_t offset, void *buffer, uint32_t length)
{
    int i = find(path);

    (void)ctx;
    if (i < 0 || tree[i].dir)
        return -1;

    uint32_t size = (uint32_t)strlen(tree[i].data);

    if (offset >= size)
        return 0;
    if (length > size - offset)
        length = size - offset;
    memcpy(buffer, tree[i].data + offset, length);
    return (int32_t)length;
}

static const mac_fs_host_t host = {NULL, fake_entry, fake_stat, fake_read};
static mac_fs_paths_t paths;

static bool pstr_is(const uint8_t *p, const char *s)
{
    return p[0] == strlen(s) && memcmp(p + 1, s, p[0]) == 0;
}

static void test_mac_names(void)
{
    uint8_t out[32];

    mac_fs_paths_mac_name("readme.txt", 20, out);
    TEST_ASSERT(pstr_is(out, "readme.txt"));
    mac_fs_paths_mac_name("a:b", 20, out);
    TEST_ASSERT(pstr_is(out, "a/b"));                   /* ':' separates Mac path names */
    mac_fs_paths_mac_name("caf\xC3\xA9.txt", 21, out);
    TEST_ASSERT(pstr_is(out, "caf\x8E.txt"));           /* U+00E9 is Mac Roman 0x8E: not changed */
    mac_fs_paths_mac_name("\xE6\x97\xA5\xE8\xA8\x98.txt", 21, out);
    TEST_ASSERT(pstr_is(out, "__.txt#21"));             /* not in Mac Roman: changed, so it gets its ID */
    mac_fs_paths_mac_name("5\xE2\x82\xAC", 22, out);
    TEST_ASSERT(pstr_is(out, "5_#22"));                 /* the euro sign: not in System 7.5's Mac Roman */
    mac_fs_paths_mac_name("bad\xC3", 23, out);
    TEST_ASSERT(pstr_is(out, "bad_#23"));               /* a broken UTF-8 sequence */
    mac_fs_paths_mac_name("a-very-long-file-name-that-does-not-fit.txt", 1234, out);
    TEST_ASSERT_EQ_INT(31, out[0]);                     /* shortened to 31 with the ID at the end */
    TEST_ASSERT(memcmp(out + 1 + 31 - 5, "#1234", 5) == 0);
    TEST_ASSERT(memcmp(out + 1, "a-very-long-file-name-th", 24) == 0);
}

static void test_tree(void)
{
    mac_fs_item_t it, again, docs;
    const mac_fs_ops_t *ops = &paths.ops;

    mac_fs_paths_init(&paths, &host, "HAL");

    /* The root is the volume. */
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, MAC_FS_ROOT_ID, &it));
    TEST_ASSERT(it.is_dir);
    TEST_ASSERT(pstr_is(it.name, "HAL"));
    TEST_ASSERT_EQ_INT(MAC_FS_ROOT_PARENT, it.parent);
    TEST_ASSERT_EQ_INT(3, it.valence);                  /* readme.txt, Docs, bin; .hidden is not shown */
    TEST_ASSERT_EQ_INT(1000 + 2082844800u, it.modified);

    /* Children in host order, hidden names skipped, IDs from MAC_FS_FIRST_ID. */
    TEST_ASSERT_EQ_INT(0, ops->child(ops->ctx, MAC_FS_ROOT_ID, 1, &it));
    TEST_ASSERT(pstr_is(it.name, "readme.txt"));
    TEST_ASSERT(!it.is_dir);
    TEST_ASSERT_EQ_INT(6, it.data_length);
    TEST_ASSERT(memcmp(it.finfo, "TEXTttxt", 8) == 0);
    TEST_ASSERT_EQ_INT(MAC_FS_FIRST_ID, it.id);
    TEST_ASSERT_EQ_INT(0, ops->child(ops->ctx, MAC_FS_ROOT_ID, 2, &docs));
    TEST_ASSERT(pstr_is(docs.name, "Docs"));
    TEST_ASSERT(docs.is_dir);
    TEST_ASSERT_EQ_INT(4, docs.valence);
    TEST_ASSERT_EQ_INT(0, ops->child(ops->ctx, MAC_FS_ROOT_ID, 3, &it));
    TEST_ASSERT(pstr_is(it.name, "bin"));
    TEST_ASSERT_EQ_INT(-1, ops->child(ops->ctx, MAC_FS_ROOT_ID, 4, &it));
    TEST_ASSERT_EQ_INT(-1, ops->child(ops->ctx, MAC_FS_ROOT_ID, 0, &it));

    /* The same item keeps its ID; lookup ignores the case of letters. */
    TEST_ASSERT_EQ_INT(0, ops->lookup(ops->ctx, MAC_FS_ROOT_ID, (const uint8_t *)"DOCS", 4, &again));
    TEST_ASSERT_EQ_INT(docs.id, again.id);
    TEST_ASSERT_EQ_INT(0, ops->get(ops->ctx, docs.id, &again));
    TEST_ASSERT(pstr_is(again.name, "Docs"));
    TEST_ASSERT_EQ_INT(-1, ops->lookup(ops->ctx, MAC_FS_ROOT_ID, (const uint8_t *)".hidden", 7, &it));
    TEST_ASSERT_EQ_INT(-1, ops->lookup(ops->ctx, MAC_FS_ROOT_ID, (const uint8_t *)"nothing", 7, &it));

    /* Inside a folder: names as the Mac sees them, and the file's contents. */
    TEST_ASSERT_EQ_INT(0, ops->lookup(ops->ctx, docs.id, (const uint8_t *)"a/b", 3, &it));
    TEST_ASSERT_EQ_INT(docs.id, it.parent);
    TEST_ASSERT(memcmp(it.finfo, "????????", 8) == 0);

    char buf[16] = {0};

    TEST_ASSERT_EQ_INT(5, ops->read(ops->ctx, it.id, false, 0, buf, sizeof buf));
    TEST_ASSERT(memcmp(buf, "colon", 5) == 0);
    TEST_ASSERT_EQ_INT(2, ops->read(ops->ctx, it.id, false, 3, buf, 2));
    TEST_ASSERT(memcmp(buf, "on", 2) == 0);
    TEST_ASSERT_EQ_INT(0, ops->read(ops->ctx, it.id, true, 0, buf, sizeof buf));    /* no resource fork */

    uint8_t name[32];

    TEST_ASSERT_EQ_INT(0, ops->child(ops->ctx, docs.id, 3, &it));
    mac_fs_paths_mac_name("caf\xC3\xA9.txt", it.id, name);
    TEST_ASSERT(memcmp(it.name, name, name[0] + 1) == 0);
    TEST_ASSERT_EQ_INT(0, ops->lookup(ops->ctx, docs.id, name + 1, name[0], &again));
    TEST_ASSERT_EQ_INT(it.id, again.id);

    /* Unknown IDs. */
    TEST_ASSERT_EQ_INT(-1, ops->get(ops->ctx, 9999, &it));
    TEST_ASSERT_EQ_INT(-1, ops->get(ops->ctx, 5, &it));
    TEST_ASSERT_EQ_INT(-1, ops->read(ops->ctx, 9999, false, 0, buf, 1));
}

int main(void)
{
    TEST_RUN(test_mac_names);
    TEST_RUN(test_tree);

    return TEST_REPORT("fs_paths");
}

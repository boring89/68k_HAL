/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "emu/mac/mac.h"

#include <string.h>

static mac_asc_t asc;
static uint8_t played[4096];
static uint32_t played_count;
static uint32_t played_rate;

static void sink(void *ctx, const uint8_t *samples, uint32_t count, uint32_t rate)
{
    (void)ctx;
    memcpy(played + played_count, samples, count);
    played_count += count;
    played_rate = rate;
}

static void reset(void)
{
    memset(&asc, 0, sizeof asc);
    played_count = 0;
}

static void write_reg(uint32_t offset, uint32_t value)
{
    mac_asc_access(&asc, offset, true, value);
}

static uint32_t read_reg(uint32_t offset)
{
    return mac_asc_access(&asc, offset, false, 0);
}

/* Cycles that make exactly 'samples' samples at 22254 Hz (a bit more than needed for the last one). */
static uint32_t cycles_for(uint32_t samples)
{
    return (uint32_t)((uint64_t)samples * MAC_CPU_HZ / 22254) + 2;
}

static void test_registers(void)
{
    reset();
    TEST_ASSERT_EQ_INT(0, read_reg(0x800)); /* the original ASC */
    write_reg(0x806, 0x60);
    TEST_ASSERT_EQ_INT(0x60, read_reg(0x806));
    write_reg(0x801, 2);
    TEST_ASSERT_EQ_INT(2, read_reg(0x801));

    /* Wave table mode: the RAM keeps what is written. */
    write_reg(0x123, 0xAB);
    TEST_ASSERT_EQ_INT(0xAB, read_reg(0x123));
}

static void test_fifo_playback(void)
{
    reset();
    write_reg(0x801, 1); /* FIFO mode */

    for (int i = 0; i < 100; i++)
    {
        write_reg(0x000, 0x90 + (uint32_t)(i & 7)); /* FIFO A */
        write_reg(0x400, 0x90 + (uint32_t)(i & 7)); /* FIFO B, the same: mono */
    }

    mac_asc_advance(&asc, cycles_for(60), sink, NULL);
    TEST_ASSERT_EQ_INT(60, played_count);
    TEST_ASSERT_EQ_INT(22254, played_rate);
    TEST_ASSERT_EQ_INT(0x90, played[0]);
    TEST_ASSERT_EQ_INT(0x97, played[7]);
    TEST_ASSERT_EQ_INT(0x90, played[8]);

    /* The remaining 40 samples, then silence (0x80) when the FIFOs run dry. */
    mac_asc_advance(&asc, cycles_for(60), sink, NULL);
    TEST_ASSERT_EQ_INT(120, played_count);
    TEST_ASSERT_EQ_INT(0x90 + (60 & 7), played[60]);
    TEST_ASSERT_EQ_INT(0x80, played[110]);
}

static void test_status_flags_and_irq(void)
{
    reset();
    write_reg(0x801, 1);
    TEST_ASSERT(!mac_asc_irq(&asc));

    /* Filling a FIFO sets the "full" flag, which is polled, not an interrupt. */
    for (int i = 0; i < 1024; i++)
    {
        TEST_ASSERT_EQ_INT(0, read_reg(0x804) & 0x08);
        write_reg(0x400, 0x80);
        write_reg(0x000, 0x80);
    }
    TEST_ASSERT_EQ_INT(0x0A, asc.status);
    TEST_ASSERT(!mac_asc_irq(&asc));
    TEST_ASSERT_EQ_INT(0x0A, read_reg(0x804)); /* reading clears */
    TEST_ASSERT_EQ_INT(0, read_reg(0x804));

    /* Playing until fewer than 512 bytes are left (513 samples played) raises the half-empty interrupt. */
    mac_asc_advance(&asc, cycles_for(513), sink, NULL);
    TEST_ASSERT(mac_asc_irq(&asc));
    TEST_ASSERT_EQ_INT(0x05, read_reg(0x804));
    TEST_ASSERT(!mac_asc_irq(&asc));

    /* Overflow: bytes beyond 1024 are lost. */
    reset();
    write_reg(0x801, 1);

    for (int i = 0; i < 1100; i++)
        write_reg(0x000, 0x81);

    TEST_ASSERT_EQ_INT(1024, asc.fifo_count[0]);

    /* "Clear FIFO". */
    write_reg(0x803, 0x80);
    TEST_ASSERT_EQ_INT(0, asc.fifo_count[0]);
}

static void test_mode_off_plays_nothing(void)
{
    reset();
    write_reg(0x000, 0x55);
    mac_asc_advance(&asc, cycles_for(100), sink, NULL);
    TEST_ASSERT_EQ_INT(0, played_count);
}

static void test_rbv_sound_interrupt(void)
{
    mac_rbv_t rbv;

    memset(&rbv, 0, sizeof rbv);
    mac_rbv_write(&rbv, 0x13, 0x80 | 0x10); /* enable the sound interrupt */
    TEST_ASSERT(!mac_rbv_irq(&rbv));
    mac_rbv_set_sound_irq(&rbv, true);
    TEST_ASSERT(mac_rbv_irq(&rbv));
    TEST_ASSERT_EQ_INT(0x90, mac_rbv_read(&rbv, 0x03, false) & 0x90);
    mac_rbv_set_sound_irq(&rbv, false);
    TEST_ASSERT(!mac_rbv_irq(&rbv));

    /* Not enabled: no interrupt, though the flag shows. */
    mac_rbv_write(&rbv, 0x13, 0x10);
    mac_rbv_set_sound_irq(&rbv, true);
    TEST_ASSERT(!mac_rbv_irq(&rbv));
    TEST_ASSERT_EQ_INT(0x10, mac_rbv_read(&rbv, 0x03, false) & 0x90);
}

int main(void)
{
    TEST_RUN(test_registers);
    TEST_RUN(test_fifo_playback);
    TEST_RUN(test_status_flags_and_irq);
    TEST_RUN(test_mode_off_plays_nothing);
    TEST_RUN(test_rbv_sound_interrupt);

    return TEST_REPORT("mac_asc");
}

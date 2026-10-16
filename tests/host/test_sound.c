/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "test.h"

#include "kernel/sound/sound.h"

#include <stdlib.h>

static void test_tone_frequency(void)
{
    enum { FRAMES = 48000 };
    int16_t *samples = malloc(FRAMES * 4);

    sound_make_tone(samples, FRAMES, 440, 50);

    /* One second of 440 Hz: about 880 sign changes. */
    int crossings = 0;

    for (int i = 1; i < FRAMES; i++)
    {
        if ((samples[2 * i] < 0) != (samples[2 * (i - 1)] < 0))
            crossings++;
    }

    TEST_ASSERT(crossings >= 870 && crossings <= 890);

    /* Both channels carry the same signal, the loudness follows the volume. */
    int peak = 0;

    for (int i = 0; i < FRAMES; i++)
    {
        TEST_ASSERT_EQ_INT(samples[2 * i], samples[2 * i + 1]);

        if (abs(samples[2 * i]) > peak)
            peak = abs(samples[2 * i]);
    }

    TEST_ASSERT(peak > 7000 && peak < 9000);

    /* The fade: starts and ends at (nearly) zero. */
    TEST_ASSERT_EQ_INT(0, samples[0]);
    TEST_ASSERT(abs(samples[2 * (FRAMES - 1)]) < 100);

    free(samples);
}

static void test_silence_and_volume(void)
{
    int16_t samples[2 * 480];

    sound_make_tone(samples, 480, 0, 100);

    for (int i = 0; i < 2 * 480; i++)
        TEST_ASSERT(abs(samples[i]) < 17000);

    sound_make_tone(samples, 480, 1000, 0);

    for (int i = 0; i < 2 * 480; i++)
        TEST_ASSERT_EQ_INT(0, samples[i]);
}

static void test_resample(void)
{
    sound_resampler_t r;
    int16_t out[2 * 64];
    const uint8_t input[] = {128, 255, 0, 128};

    sound_resampler_reset(&r);

    /* 12000 Hz -> 48000 Hz: each input sample is repeated 4 times. */
    uint32_t frames = sound_resample_u8(&r, input, 4, 12000, out, 64, 100);

    TEST_ASSERT_EQ_INT(16, frames);
    TEST_ASSERT_EQ_INT(0, out[0]);
    TEST_ASSERT_EQ_INT(127 * 256, out[2 * 4]);
    TEST_ASSERT_EQ_INT(-128 * 256, out[2 * 8]);
    TEST_ASSERT_EQ_INT(out[2 * 8], out[2 * 8 + 1]);

    /* Half volume halves the samples; the output size is limited. */
    sound_resampler_reset(&r);
    frames = sound_resample_u8(&r, input, 4, 12000, out, 6, 50);
    TEST_ASSERT_EQ_INT(6, frames);
    TEST_ASSERT_EQ_INT(127 * 128, out[2 * 4]);

    TEST_ASSERT_EQ_INT(0, sound_resample_u8(&r, input, 0, 12000, out, 6, 50));
    TEST_ASSERT_EQ_INT(0, sound_resample_u8(&r, input, 4, 0, out, 6, 50));
}

/* Chunks of any size give the same stream as one big chunk: no clicks at the joins. */
static void test_resample_chunks_join(void)
{
    enum { TOTAL = 3000 };
    uint8_t input[TOTAL];
    static int16_t whole[2 * 20000];
    static int16_t parts[2 * 20000];
    sound_resampler_t a;
    sound_resampler_t b;

    for (int i = 0; i < TOTAL; i++)
        input[i] = (uint8_t)(128 + 100 * ((i / 17) % 2 ? 1 : -1));

    sound_resampler_reset(&a);
    sound_resampler_reset(&b);

    uint32_t whole_frames = sound_resample_u8(&a, input, TOTAL, 22254, whole, 20000, 100);
    uint32_t done = 0;
    uint32_t offset = 0;
    uint32_t sizes[] = {371, 370, 371, 1, 7, 500, 2, 1000, 378};

    for (unsigned int k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
    {
        done += sound_resample_u8(&b, input + offset, sizes[k], 22254, parts + 2 * done, 20000 - done, 100);
        offset += sizes[k];
    }

    TEST_ASSERT_EQ_INT(TOTAL, offset);
    TEST_ASSERT_EQ_INT(whole_frames, done);
    TEST_ASSERT(whole_frames > 6400 && whole_frames < 6500); /* 3000 * 48000 / 22254 */

    for (uint32_t i = 0; i < done; i++)
        TEST_ASSERT_EQ_INT(whole[2 * i], parts[2 * i]);
}

int main(void)
{
    TEST_RUN(test_tone_frequency);
    TEST_RUN(test_silence_and_volume);
    TEST_RUN(test_resample);
    TEST_RUN(test_resample_chunks_join);

    return TEST_REPORT("sound");
}

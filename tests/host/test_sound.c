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
    /* 22254 Hz Mac samples to 48000 Hz: 2.157 times as many frames. */
    TEST_ASSERT_EQ_INT(48000, sound_resampled_frames(22254, 22254));
    TEST_ASSERT_EQ_INT(0, sound_resampled_frames(100, 0));

    const uint8_t input[] = {128, 255, 0, 128};
    int16_t output[2 * 16];
    uint32_t frames = sound_resample_u8_mono(input, 4, 12000, output, 16, 100);

    /* 12000 Hz -> 48000 Hz: each input sample is repeated 4 times. */
    TEST_ASSERT_EQ_INT(16, frames);
    TEST_ASSERT_EQ_INT(0, output[0]);
    TEST_ASSERT_EQ_INT(127 * 256, output[2 * 4]);
    TEST_ASSERT_EQ_INT(-128 * 256, output[2 * 8]);
    TEST_ASSERT_EQ_INT(output[2 * 8], output[2 * 8 + 1]);

    /* Half volume halves the samples; the output size is limited. */
    frames = sound_resample_u8_mono(input, 4, 12000, output, 6, 50);
    TEST_ASSERT_EQ_INT(6, frames);
    TEST_ASSERT_EQ_INT(127 * 128, output[2 * 4]);
}

int main(void)
{
    TEST_RUN(test_tone_frequency);
    TEST_RUN(test_silence_and_volume);
    TEST_RUN(test_resample);

    return TEST_REPORT("sound");
}

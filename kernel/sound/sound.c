/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sound.h"

#include "kernel/memory/heap.h"

#include <stddef.h>

static const sound_device_t *device;

int sound_register(const sound_device_t *new_device)
{
    if (device != NULL)
        return -1;

    device = new_device;

    return 0;
}

const sound_device_t *sound_get(void)
{
    return device;
}

void sound_make_tone(int16_t *samples, uint32_t frames, uint32_t frequency, uint32_t volume)
{
    if (volume > 100)
        volume = 100;

    int32_t amplitude = (int32_t)(32767 * volume / 100);

    /* 5 ms ramps up and down: a click-free start and end. */
    uint32_t ramp = SOUND_RATE / 200;

    if (ramp * 2 > frames)
        ramp = frames / 2;

    /* A square wave: the sign flips every half period (period = rate / frequency). */
    uint32_t half_period_x2 = frequency == 0 ? 0 : SOUND_RATE; /* compare 2 * frequency * n against rate */
    uint32_t phase = 0;
    int32_t level = amplitude / 2; /* a full-scale square wave is very loud */

    for (uint32_t i = 0; i < frames; i++)
    {
        int32_t value = level;

        if (frequency != 0)
        {
            /* phase counts in units of 1/(2*rate) period; flip when it passes rate. */
            phase += 2 * frequency;

            if (phase >= half_period_x2)
            {
                phase -= half_period_x2;
                level = -level;
                value = level;
            }
        }

        if (i < ramp)
            value = value * (int32_t)i / (int32_t)ramp;
        else if (i >= frames - ramp)
            value = value * (int32_t)(frames - 1 - i) / (int32_t)ramp;

        samples[2 * i] = (int16_t)value;
        samples[2 * i + 1] = (int16_t)value;
    }
}

int sound_beep(uint32_t frequency, uint32_t milliseconds, uint32_t volume)
{
    if (device == NULL)
        return -1;

    uint32_t frames = SOUND_RATE / 1000 * milliseconds;
    int16_t *samples = kmalloc((size_t)frames * 4);

    if (samples == NULL)
        return -1;

    sound_make_tone(samples, frames, frequency, volume);

    int status = device->play(samples, frames);

    kfree(samples);

    return status;
}

uint32_t sound_resampled_frames(uint32_t input_count, uint32_t source_rate)
{
    if (source_rate == 0)
        return 0;

    return (uint32_t)((uint64_t)input_count * SOUND_RATE / source_rate);
}

uint32_t sound_resample_u8_mono(const uint8_t *input, uint32_t input_count, uint32_t source_rate,
                                int16_t *output, uint32_t out_frames, uint32_t volume)
{
    uint32_t frames = sound_resampled_frames(input_count, source_rate);

    if (frames > out_frames)
        frames = out_frames;

    if (volume > 100)
        volume = 100;

    for (uint32_t i = 0; i < frames; i++)
    {
        uint32_t source = (uint32_t)((uint64_t)i * source_rate / SOUND_RATE);

        if (source >= input_count)
            source = input_count - 1;

        /* Unsigned 8-bit: 0x80 is silence. Scale to 16 bits and apply the volume. */
        int32_t value = ((int32_t)input[source] - 128) * 256 * (int32_t)volume / 100;

        output[2 * i] = (int16_t)value;
        output[2 * i + 1] = (int16_t)value;
    }

    return frames;
}

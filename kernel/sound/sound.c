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

uint32_t sound_write(const int16_t *samples, uint32_t frames)
{
    if (device == NULL || device->write == NULL)
        return 0;

    return device->write(samples, frames);
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

void sound_resampler_reset(sound_resampler_t *resampler)
{
    resampler->rate = 0;
    resampler->in_total = 0;
    resampler->out_total = 0;
}

uint32_t sound_resample_u8(sound_resampler_t *resampler, const uint8_t *input, uint32_t count,
                           uint32_t source_rate, int16_t *out, uint32_t max_frames, uint32_t volume)
{
    if (source_rate == 0 || count == 0)
        return 0;

    if (resampler->rate != source_rate)
    {
        resampler->rate = source_rate;
        resampler->in_total = 0;
        resampler->out_total = 0;
    }

    if (volume > 100)
        volume = 100;

    /* After this chunk the stream has consumed in_end samples and should have produced out_end frames. */
    uint64_t in_end = resampler->in_total + count;
    uint64_t out_end = in_end * SOUND_RATE / source_rate;
    uint32_t frames = (uint32_t)(out_end - resampler->out_total);

    if (frames > max_frames)
        frames = max_frames;

    for (uint32_t i = 0; i < frames; i++)
    {
        uint64_t global = (resampler->out_total + i) * source_rate / SOUND_RATE;
        uint32_t local = global < resampler->in_total ? 0 : (uint32_t)(global - resampler->in_total);

        if (local >= count)
            local = count - 1;

        /* Unsigned 8-bit: 0x80 is silence. Scale to 16 bits and apply the volume. */
        int32_t value = ((int32_t)input[local] - 128) * 256 * (int32_t)volume / 100;

        out[2 * i] = (int16_t)value;
        out[2 * i + 1] = (int16_t)value;
    }

    resampler->in_total = in_end;
    resampler->out_total += frames;

    return frames;
}

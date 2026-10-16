/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef KERNEL_SOUND_SOUND_H
#define KERNEL_SOUND_SOUND_H

#include <stdint.h>

/*
 * Sound output. A sound card driver registers a device that plays
 * 16-bit signed stereo samples at 48000 frames per second (one frame =
 * a left and a right sample); everything above it (beeps, the Mac's
 * sound chip) produces that format.
 */

#define SOUND_RATE 48000

typedef struct
{
    const char *name;
    /*
     * Play frames frames (interleaved left/right samples) and return when
     * they have been played. Returns 0, or -1 on error.
     */
    int (*play)(const int16_t *samples, uint32_t frames);
} sound_device_t;

/* Make device the sound output. Returns 0, or -1 if there already is one. */
int sound_register(const sound_device_t *device);

/* The sound output, or NULL. */
const sound_device_t *sound_get(void);

/* Play a tone of frequency Hz for milliseconds (square wave, volume 0..100). Returns 0, or -1 without a sound device. */
int sound_beep(uint32_t frequency, uint32_t milliseconds, uint32_t volume);

/*
 * Fill frames frames of stereo samples with a tone: a square wave of
 * frequency Hz, at volume percent of full scale, with a short fade in
 * and out so that it does not click. Pure function.
 */
void sound_make_tone(int16_t *samples, uint32_t frames, uint32_t frequency, uint32_t volume);

/*
 * Convert unsigned 8-bit mono samples at source_rate to 48000 Hz
 * signed 16-bit stereo (nearest sample), as the Mac's sound chip
 * delivers them. Produces up to out_frames frames and returns how many
 * frames the input makes (rounded down); the caller sizes the output
 * with sound_resampled_frames. volume is 0..100.
 */
uint32_t sound_resample_u8_mono(const uint8_t *input, uint32_t input_count, uint32_t source_rate,
                                int16_t *output, uint32_t out_frames, uint32_t volume);

/* Frames sound_resample_u8_mono makes from input_count samples. */
uint32_t sound_resampled_frames(uint32_t input_count, uint32_t source_rate);

#endif

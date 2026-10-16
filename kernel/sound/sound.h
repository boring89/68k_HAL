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
    /*
     * Streaming: queue frames frames behind what is already playing and
     * return at once. Returns how many frames were taken (less than frames
     * if the device's buffer is full: ask again later). The first call starts
     * the stream; when the queue runs dry the device plays silence.
     */
    uint32_t (*write)(const int16_t *samples, uint32_t frames);
} sound_device_t;

/* Make device the sound output. Returns 0, or -1 if there already is one. */
int sound_register(const sound_device_t *device);

/* The sound output, or NULL. */
const sound_device_t *sound_get(void);

/* Queue frames on the streaming output (see sound_device_t.write). Returns how many were taken; 0 without a sound device. */
uint32_t sound_write(const int16_t *samples, uint32_t frames);

/* Play a tone of frequency Hz for milliseconds (square wave, volume 0..100). Returns 0, or -1 without a sound device. */
int sound_beep(uint32_t frequency, uint32_t milliseconds, uint32_t volume);

/*
 * Fill frames frames of stereo samples with a tone: a square wave of
 * frequency Hz, at volume percent of full scale, with a short fade in
 * and out so that it does not click. Pure function.
 */
void sound_make_tone(int16_t *samples, uint32_t frames, uint32_t frequency, uint32_t volume);

/*
 * Converts the Mac's unsigned 8-bit mono samples at a rate of its own
 * (22254 Hz, ...) to 48000 Hz signed 16-bit stereo, a chunk at a time.
 * It remembers where it is in the stream, so consecutive chunks join up
 * with no click or gap even though a chunk rarely makes a whole number of
 * output frames. Nearest-sample, no filtering.
 */
typedef struct
{
    uint32_t rate;       /* the source rate of the samples converted so far */
    uint64_t in_total;   /* source samples consumed */
    uint64_t out_total;  /* frames produced */
} sound_resampler_t;

void sound_resampler_reset(sound_resampler_t *resampler);

/*
 * Convert count samples (0x80 = silence) at source_rate; volume is 0..100.
 * Writes at most max_frames frames to out and returns how many it
 * made (about count * 48000 / source_rate; a chunk that makes more than
 * max_frames loses the rest). A change of rate starts a new stream.
 */
uint32_t sound_resample_u8(sound_resampler_t *resampler, const uint8_t *input, uint32_t count,
                           uint32_t source_rate, int16_t *out, uint32_t max_frames, uint32_t volume);

#endif

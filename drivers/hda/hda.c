/*
 * Copyright (C) 2026 YuanChi Hsieh
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hda.h"

#include "hal/dma.h"
#include "hal/timer.h"

#include "kernel/lib/k_memory.h"
#include "kernel/printf/printf.h"
#include "kernel/sound/sound.h"

/*
 * Controller registers (HDA specification 3.3, "Register Set"); offsets
 * from BAR 0. Width in bits in brackets.
 */
#define REG_GCAP 0x00      /* [16] bits 15:12 output streams, 11:8 input streams */
#define REG_GCTL 0x08      /* [32] bit 0: CRST, 0 = controller held in reset */
#define REG_STATESTS 0x0E  /* [16] bit n set = codec n responded after reset */
#define REG_INTCTL 0x20
#define REG_CORBLBASE 0x40
#define REG_CORBUBASE 0x44
#define REG_CORBWP 0x48    /* [16] write pointer (driver) */
#define REG_CORBRP 0x4A    /* [16] read pointer (controller), bit 15 = reset */
#define REG_CORBCTL 0x4C   /* [8] bit 1: DMA run */
#define REG_CORBSIZE 0x4E  /* [8] 0 = 2 entries, 1 = 16, 2 = 256 */
#define REG_RIRBLBASE 0x50
#define REG_RIRBUBASE 0x54
#define REG_RIRBWP 0x58    /* [16] write pointer (controller), bit 15 = reset */
#define REG_RINTCNT 0x5A
#define REG_RIRBCTL 0x5C   /* [8] bit 1: DMA run */
#define REG_RIRBSTS 0x5D   /* [8] bit 0: response interrupt flag */
#define REG_RIRBSIZE 0x5E
#define REG_ICOI 0x60      /* [32] immediate command output */
#define REG_ICII 0x64      /* [32] immediate response input */
#define REG_ICIS 0x68      /* [16] bit 0 busy, bit 1 response valid */
#define REG_STREAM 0x80    /* first stream descriptor; each is 0x20 bytes */

/* Stream descriptor registers (offsets inside one descriptor). */
#define SD_CTL 0x00        /* [24] bit 0 SRST, bit 1 RUN, bits 23:20 stream tag */
#define SD_LPIB 0x04       /* [32] link position in buffer, in bytes */
#define SD_CBL 0x08        /* [32] cyclic buffer length in bytes */
#define SD_LVI 0x0C        /* [16] last valid index of the buffer descriptor list */
#define SD_FMT 0x12        /* [16] stream format */
#define SD_BDLPL 0x18
#define SD_BDLPU 0x1C

#define GCTL_CRST 0x1u
#define SD_CTL_SRST 0x1u
#define SD_CTL_RUN 0x2u

/* The format: 48 kHz base, 16 bits, 2 channels (specification 3.7.1, Figure 56). */
#define STREAM_FORMAT 0x0011
#define STREAM_TAG 1

/* Codec verbs (specification 7.3.3). */
#define VERB_GET_PARAMETER 0xF00
#define VERB_GET_CONNECTION_LIST 0xF02
#define VERB_GET_CONFIG_DEFAULT 0xF1C
#define VERB_SET_POWER_STATE 0x705
#define VERB_SET_STREAM_CHANNEL 0x706
#define VERB_SET_PIN_CONTROL 0x707
#define VERB_SET_EAPD 0x70C
#define VERB_SET_CONNECTION_SELECT 0x701
#define VERB_SET_FORMAT 0x2    /* 4-bit verb with a 16-bit payload */
#define VERB_SET_AMP 0x3

/* Parameters for VERB_GET_PARAMETER (specification 7.3.4). */
#define PARAM_NODE_COUNT 0x04
#define PARAM_FUNCTION_TYPE 0x05
#define PARAM_WIDGET_CAPS 0x09
#define PARAM_PIN_CAPS 0x0C
#define PARAM_CONNECTION_LENGTH 0x0E
#define PARAM_AMP_CAPS_OUT 0x12

#define WIDGET_AUDIO_OUTPUT 0x0
#define WIDGET_MIXER 0x2
#define WIDGET_SELECTOR 0x3
#define WIDGET_PIN 0x4

#define CORB_ENTRIES 256
#define RIRB_ENTRIES 256

/* Buffer descriptor list entry (specification 3.6.2). */
typedef struct
{
    uint64_t address;
    uint32_t length;
    uint32_t flags; /* bit 0: interrupt on completion */
} __attribute__((packed)) bdl_entry_t;

#define BDL_ENTRIES 8
#define BDL_CHUNK 16384 /* bytes per entry */
#define RING_BYTES (BDL_ENTRIES * BDL_CHUNK)

typedef struct
{
    volatile uint8_t *registers;
    uint32_t *corb;
    uint64_t *rirb;
    unsigned int rirb_read;
    unsigned int codec;
    unsigned int stream_offset; /* of the first output stream descriptor */
    uint8_t *ring;              /* audio data DMA buffer */
    int immediate; /* use the immediate command registers instead of the rings */
    int ready;
} hda_t;

static hda_t hda;

static uint32_t reg32(uint32_t offset)
{
    return *(volatile uint32_t *)(hda.registers + offset);
}

static uint16_t reg16(uint32_t offset)
{
    return *(volatile uint16_t *)(hda.registers + offset);
}

static void set32(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(hda.registers + offset) = value;
}

static void set16(uint32_t offset, uint16_t value)
{
    *(volatile uint16_t *)(hda.registers + offset) = value;
}

static void set8(uint32_t offset, uint8_t value)
{
    *(volatile uint8_t *)(hda.registers + offset) = value;
}

static void delay_us(uint32_t us)
{
    uint64_t end = hal_timer_ns() + (uint64_t)us * 1000u;

    while (hal_timer_ns() < end)
        ;
}

/* Wait until (reg & mask) == value, up to 100 ms. Returns 0 or -1. */
static int wait_bits16(uint32_t offset, uint16_t mask, uint16_t value)
{
    uint64_t end = hal_timer_ns() + 100000000u;

    while ((reg16(offset) & mask) != value)
    {
        if (hal_timer_ns() > end)
            return -1;
    }

    return 0;
}

static int wait_bits32(uint32_t offset, uint32_t mask, uint32_t value)
{
    uint64_t end = hal_timer_ns() + 100000000u;

    while ((reg32(offset) & mask) != value)
    {
        if (hal_timer_ns() > end)
            return -1;
    }

    return 0;
}

/* --- talking to the codec: CORB (commands out) and RIRB (responses in) --- */

/*
 * The "immediate command" registers (specification 3.3.38): write the
 * verb, set the busy bit, wait for the valid bit, read the response.
 * One command at a time, no rings. Used if the CORB/RIRB rings do not
 * work (QEMU's controller stops fetching commands after the first
 * response with the settings below; see setup_command_rings).
 */
static int immediate_command(uint32_t verb, uint32_t *response)
{
    uint64_t end = hal_timer_ns() + 100000000u;

    set16(REG_ICIS, 0x02); /* clear a stale "valid" flag */
    set32(REG_ICOI, verb);
    set16(REG_ICIS, 0x01); /* ICB: send */

    while ((reg16(REG_ICIS) & 0x03) != 0x02) /* busy cleared, valid set */
    {
        if (hal_timer_ns() > end)
            return -1;
    }

    if (response != NULL)
        *response = reg32(REG_ICII);

    return 0;
}

/* Send one 32-bit verb and wait for its response. Returns 0 or -1. */
static int codec_command(uint32_t verb, uint32_t *response)
{
    if (hda.immediate)
        return immediate_command(verb, response);
    unsigned int write = (reg16(REG_CORBWP) + 1) & (CORB_ENTRIES - 1);

    hda.corb[write] = verb;
    set16(REG_CORBWP, (uint16_t)write);

    uint64_t end = hal_timer_ns() + 100000000u;

    /* The controller advances RIRBWP once the response has been stored. */
    while ((reg16(REG_RIRBWP) & (RIRB_ENTRIES - 1)) == hda.rirb_read)
    {
        if (hal_timer_ns() > end)
            return -1;
    }

    hda.rirb_read = (hda.rirb_read + 1) & (RIRB_ENTRIES - 1);

    /*
     * Acknowledge the response interrupt flag (RIRBSTS bit 0, write 1 to
     * clear), although interrupts are not used: the controller stops
     * fetching commands once RINTCNT responses are unacknowledged
     * (specification 3.3.28).
     */
    set8(REG_RIRBSTS, 0x01);

    if (response != NULL)
        *response = (uint32_t)hda.rirb[hda.rirb_read];

    return 0;
}

/* A verb with a 12-bit identifier and 8-bit payload, or (verb < 0x10) a 4-bit identifier and a 16-bit payload. */
static uint32_t make_verb(uint32_t nid, uint32_t verb, uint32_t payload)
{
    if (verb < 0x10)
        return (hda.codec << 28) | (nid << 20) | (verb << 16) | (payload & 0xFFFF);

    return (hda.codec << 28) | (nid << 20) | (verb << 8) | (payload & 0xFF);
}

static uint32_t query(uint32_t nid, uint32_t verb, uint32_t payload)
{
    uint32_t response = 0;

    codec_command(make_verb(nid, verb, payload), &response);

    return response;
}

static void command(uint32_t nid, uint32_t verb, uint32_t payload)
{
    codec_command(make_verb(nid, verb, payload), NULL);
}

static int setup_command_rings(void)
{
    uint32_t phys;
    uint8_t *memory = hal_dma_alloc(4096, &phys); /* CORB 1 KiB, then the RIRB 2 KiB at +2048 */

    if (memory == NULL)
        return -1;

    hda.corb = (uint32_t *)memory;
    hda.rirb = (uint64_t *)(memory + 2048);

    /* CORB: stop DMA, 256 entries, base address, reset the read pointer. */
    set8(REG_CORBCTL, 0);
    set8(REG_CORBSIZE, 2);
    set32(REG_CORBLBASE, phys);
    set32(REG_CORBUBASE, 0);
    set16(REG_CORBRP, 0x8000);

    if (wait_bits16(REG_CORBRP, 0x8000, 0x8000) != 0)
        return -1;

    set16(REG_CORBRP, 0);

    if (wait_bits16(REG_CORBRP, 0x8000, 0) != 0)
        return -1;

    set16(REG_CORBWP, 0);
    set8(REG_CORBCTL, 0x02);

    /* RIRB: the same; one response per interrupt, but nothing uses the interrupt. */
    set8(REG_RIRBCTL, 0);
    set8(REG_RIRBSIZE, 2);
    set32(REG_RIRBLBASE, phys + 2048);
    set32(REG_RIRBUBASE, 0);
    set16(REG_RIRBWP, 0x8000);
    set16(REG_RINTCNT, 1);
    hda.rirb_read = 0;
    set8(REG_RIRBCTL, 0x02);

    return 0;
}

/* --- finding the output path in the codec --- */

typedef struct
{
    uint32_t afg;
    uint32_t dac;
    uint32_t pin;
    uint32_t middle; /* a mixer or selector between them, 0 if none */
} path_t;

static uint32_t widget_type(uint32_t nid)
{
    return (query(nid, VERB_GET_PARAMETER, PARAM_WIDGET_CAPS) >> 20) & 0xF;
}

/* Number of inputs of a widget and the nid of input index (short and long form lists). */
static uint32_t connection(uint32_t nid, uint32_t index, uint32_t *count_out)
{
    uint32_t length_info = query(nid, VERB_GET_PARAMETER, PARAM_CONNECTION_LENGTH);
    int long_form = (length_info & 0x80) != 0;
    uint32_t count = length_info & 0x7F;

    if (count_out != NULL)
        *count_out = count;

    if (index >= count)
        return 0;

    uint32_t per_response = long_form ? 2 : 4;
    uint32_t response = query(nid, VERB_GET_CONNECTION_LIST, (index / per_response) * per_response);
    uint32_t shift = (index % per_response) * (long_form ? 16 : 8);

    return (response >> shift) & (long_form ? 0x7FFF : 0x7F);
}

/* Is there an audio output widget behind pin, directly or through one mixer/selector? */
static int trace_pin(uint32_t pin, path_t *path)
{
    uint32_t count = 0;

    connection(pin, 0, &count);

    for (uint32_t i = 0; i < count; i++)
    {
        uint32_t source = connection(pin, i, NULL);
        uint32_t type = widget_type(source);

        if (type == WIDGET_AUDIO_OUTPUT)
        {
            path->dac = source;
            path->middle = 0;
            path->pin = pin;

            return 0;
        }

        if (type == WIDGET_MIXER || type == WIDGET_SELECTOR)
        {
            uint32_t inner_count = 0;

            connection(source, 0, &inner_count);

            for (uint32_t j = 0; j < inner_count; j++)
            {
                uint32_t inner = connection(source, j, NULL);

                if (widget_type(inner) == WIDGET_AUDIO_OUTPUT)
                {
                    path->dac = inner;
                    path->middle = source;
                    path->pin = pin;
                    command(pin, VERB_SET_CONNECTION_SELECT, i);

                    return 0;
                }
            }
        }
    }

    return -1;
}

static int find_output_path(path_t *path)
{
    uint32_t groups = query(0, VERB_GET_PARAMETER, PARAM_NODE_COUNT);
    uint32_t first_group = (groups >> 16) & 0xFF;
    uint32_t group_count = groups & 0xFF;

    for (uint32_t g = first_group; g < first_group + group_count; g++)
    {
        if ((query(g, VERB_GET_PARAMETER, PARAM_FUNCTION_TYPE) & 0xFF) != 1)
            continue; /* not the audio function group */

        uint32_t widgets = query(g, VERB_GET_PARAMETER, PARAM_NODE_COUNT);
        uint32_t first = (widgets >> 16) & 0xFF;
        uint32_t count = widgets & 0xFF;

        path->afg = g;

        for (uint32_t w = first; w < first + count; w++)
        {
            if (widget_type(w) != WIDGET_PIN)
                continue;

            /* Pin configuration default: bits 31:30 connectivity (1 = no connection), 23:20 device. */
            uint32_t config = query(w, VERB_GET_CONFIG_DEFAULT, 0);
            uint32_t connectivity = config >> 30;
            uint32_t device = (config >> 20) & 0xF;

            if (connectivity == 1 || device > 2) /* line out, speaker, headphone out only */
                continue;

            if (trace_pin(w, path) == 0)
                return 0;
        }
    }

    return -1;
}

/* Unmute a widget's output amplifier and all its input amplifiers at 0 dB. */
static void unmute(uint32_t afg, uint32_t nid)
{
    uint32_t caps = query(nid, VERB_GET_PARAMETER, PARAM_WIDGET_CAPS);
    uint32_t amp_caps = query((caps & 0x8) ? nid : afg, VERB_GET_PARAMETER, PARAM_AMP_CAPS_OUT);
    uint32_t zero_db = amp_caps & 0x7F;

    if (caps & 0x4)
        command(nid, VERB_SET_AMP, 0xB000 | zero_db); /* output amp, left + right */

    if (caps & 0x2)
    {
        uint32_t inputs = 0;

        connection(nid, 0, &inputs);

        for (uint32_t i = 0; i < (inputs ? inputs : 1) && i < 16; i++)
            command(nid, VERB_SET_AMP, 0x7000 | (i << 8) | zero_db); /* input amp, left + right */
    }
}

static int prepare_codec(void)
{
    path_t path;

    if (find_output_path(&path) != 0)
        return -1;

    command(path.afg, VERB_SET_POWER_STATE, 0);

    unsigned int nodes[3] = {path.dac, path.middle, path.pin};

    for (int i = 0; i < 3; i++)
    {
        if (nodes[i] == 0)
            continue;

        command(nodes[i], VERB_SET_POWER_STATE, 0);
        unmute(path.afg, nodes[i]);
    }

    /* The converter: take stream 1, channel 0, in our format. */
    command(path.dac, VERB_SET_STREAM_CHANNEL, (STREAM_TAG << 4) | 0);
    codec_command(make_verb(path.dac, VERB_SET_FORMAT, STREAM_FORMAT), NULL);

    /* The pin: output enabled, and the external amplifier on if there is one. */
    command(path.pin, VERB_SET_PIN_CONTROL, 0x40);
    command(path.pin, VERB_SET_EAPD, 0x02);

    return 0;
}

/* --- the output stream --- */

static uint32_t stream(uint32_t offset)
{
    return REG_STREAM + hda.stream_offset + offset;
}

static int hda_play(const int16_t *samples, uint32_t frames)
{
    if (!hda.ready || frames == 0)
        return -1;

    const uint8_t *data = (const uint8_t *)samples;
    uint32_t total = frames * 4;
    uint32_t written = 0;

    /* Fill the whole ring, padding with silence after the last sample. */
    uint32_t first = total < RING_BYTES ? total : RING_BYTES;

    k_memcpy(hda.ring, data, first);
    k_memset(hda.ring + first, 0, RING_BYTES - first);
    written = first;

    set32(stream(SD_LPIB), 0);
    set32(stream(SD_CTL), (reg32(stream(SD_CTL)) & 0xFFFFFFu) | SD_CTL_RUN);

    /* Follow the play position; refill the part the controller has finished with. */
    uint64_t guard = hal_timer_ns() + (uint64_t)(total / 4 + SOUND_RATE) * 1000000000ull / SOUND_RATE + 500000000ull;
    uint32_t last_position = 0;
    uint32_t played = 0; /* total bytes the controller has consumed */
    uint32_t filled_to = RING_BYTES < total ? RING_BYTES : total; /* data bytes the ring holds ahead */

    (void)filled_to;

    while (played < total)
    {
        uint32_t position = reg32(stream(SD_LPIB));

        if (position != last_position)
        {
            uint32_t advance = position >= last_position ? position - last_position
                                                         : RING_BYTES - last_position + position;

            played += advance;

            /* The `advance` bytes just behind the play position are free: refill them. */
            uint32_t area = (position + RING_BYTES - advance) % RING_BYTES;

            for (uint32_t i = 0; i < advance; i++)
            {
                uint8_t value = written < total ? data[written] : 0;

                hda.ring[(area + i) % RING_BYTES] = value;
                written++;
            }

            last_position = position;
        }

        if (hal_timer_ns() > guard)
            break;
    }

    set32(stream(SD_CTL), reg32(stream(SD_CTL)) & ~SD_CTL_RUN & 0xFFFFFFu);

    return played >= total ? 0 : -1;
}

static const sound_device_t hda_sound = {
    .name = "hda",
    .play = hda_play,
};

static int setup_stream(void)
{
    uint32_t caps = reg16(REG_GCAP);
    uint32_t input_streams = (caps >> 8) & 0xF;
    uint32_t output_streams = (caps >> 12) & 0xF;

    if (output_streams == 0)
        return -1;

    hda.stream_offset = input_streams * 0x20; /* output streams come after the input streams */

    uint32_t ring_phys;
    uint32_t list_phys;
    bdl_entry_t *list;

    hda.ring = hal_dma_alloc(RING_BYTES, &ring_phys);
    list = hal_dma_alloc(BDL_ENTRIES * sizeof(bdl_entry_t), &list_phys);

    if (hda.ring == NULL || list == NULL)
        return -1;

    for (int i = 0; i < BDL_ENTRIES; i++)
    {
        list[i].address = ring_phys + (uint32_t)i * BDL_CHUNK;
        list[i].length = BDL_CHUNK;
        list[i].flags = 0;
    }

    /* Reset the stream descriptor, then program it (specification 4.5.1). */
    set32(stream(SD_CTL), SD_CTL_SRST);

    if (wait_bits32(stream(SD_CTL), SD_CTL_SRST, SD_CTL_SRST) != 0)
        return -1;

    set32(stream(SD_CTL), 0);

    if (wait_bits32(stream(SD_CTL), SD_CTL_SRST, 0) != 0)
        return -1;

    set32(stream(SD_CTL), (uint32_t)STREAM_TAG << 20);
    set32(stream(SD_CBL), RING_BYTES);
    set16(stream(SD_LVI), BDL_ENTRIES - 1);
    set16(stream(SD_FMT), STREAM_FORMAT);
    set32(stream(SD_BDLPL), list_phys);
    set32(stream(SD_BDLPU), 0);

    return 0;
}

static int hda_probe(const hal_pci_device_t *pci)
{
    if (hda.registers != NULL)
        return -1;

    hal_pci_enable(pci, HAL_PCI_COMMAND_MEMORY | HAL_PCI_COMMAND_BUS_MASTER);

    hda.registers = hal_pci_map_bar(pci, 0);

    if (hda.registers == NULL)
        return -1;

    /* Reset the controller: CRST low, then high; wait for the codecs to announce themselves. */
    set32(REG_GCTL, 0);

    if (wait_bits32(REG_GCTL, GCTL_CRST, 0) != 0)
        goto fail;

    delay_us(200);
    set32(REG_GCTL, GCTL_CRST);

    if (wait_bits32(REG_GCTL, GCTL_CRST, GCTL_CRST) != 0)
        goto fail;

    delay_us(2000); /* codecs need up to 521 us after reset (specification 4.3) */

    uint16_t codecs = reg16(REG_STATESTS) & 0x7FFF;

    if (codecs == 0)
    {
        kernel_printf("hda: no codec\n");
        goto fail;
    }

    for (hda.codec = 0; !(codecs & (1u << hda.codec)); hda.codec++)
        ;

    if (setup_command_rings() != 0)
    {
        kernel_printf("hda: cannot start the command rings\n");
        goto fail;
    }

    /* Two commands in a row: QEMU's controller answers only the first through the rings. */
    uint32_t first;
    uint32_t second;

    if (codec_command(make_verb(0, VERB_GET_PARAMETER, 0), &first) != 0 ||
        codec_command(make_verb(0, VERB_GET_PARAMETER, 2), &second) != 0)
        hda.immediate = 1;

    if (prepare_codec() != 0)
    {
        kernel_printf("hda: no output path in codec %u\n", hda.codec);
        goto fail;
    }

    if (setup_stream() != 0)
    {
        kernel_printf("hda: cannot set up the output stream\n");
        goto fail;
    }

    hda.ready = 1;

    return sound_register(&hda_sound);

fail:
    hda.registers = NULL;

    return -1;
}

const hal_pci_driver_t hda_pci_driver = {
    .name = "hda",
    .vendor_id = 0,
    .class_code = 0x04,
    .subclass = 0x03,
    .probe = hda_probe,
};

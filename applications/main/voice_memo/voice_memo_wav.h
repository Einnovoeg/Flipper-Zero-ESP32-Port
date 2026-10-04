#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct File File;

/** Write a canonical 44-byte WAV header with placeholder sizes. */
bool voice_memo_wav_write_header(File* file, uint32_t sample_rate);

/** Rewrite the header with final sizes. File must be open for writing. */
bool voice_memo_wav_finalize(File* file, uint32_t sample_rate, uint32_t data_bytes);

/** Validate a WAV header, returning rate/channels/data size. Advances the
 * file position past the header (and any prefix chunks) to the data start.
 * Accepts 8/16-bit mono/stereo PCM. */
bool voice_memo_wav_read_header(
    File* file,
    uint32_t* sample_rate,
    uint16_t* channels,
    uint16_t* bits,
    uint32_t* data_bytes);

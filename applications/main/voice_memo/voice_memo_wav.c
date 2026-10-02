#include "voice_memo_wav.h"

#include <storage/storage.h>
#include <string.h>

#define TAG "VoiceMemoWav"

/* Canonical PCM header, 44 bytes. Written twice: once blank at open, once
 * with final sizes on stop (storage has no truncate/extend issues since we
 * always write forward). */
#pragma pack(push, 1)
typedef struct {
    char riff[4]; /* "RIFF" */
    uint32_t size; /* file size - 8 */
    char wave[4]; /* "WAVE" */
    char fmt[4]; /* "fmt " */
    uint32_t fmt_len; /* 16 */
    uint16_t format; /* 1 = PCM */
    uint16_t channels;
    uint32_t rate;
    uint32_t byte_rate;
    uint16_t align;
    uint16_t bits;
    char data[4]; /* "data" */
    uint32_t data_size;
} WavHeader;
#pragma pack(pop)

static void wav_fill(WavHeader* h, uint32_t rate, uint32_t data_bytes) {
    memcpy(h->riff, "RIFF", 4);
    h->size = 36 + data_bytes;
    memcpy(h->wave, "WAVE", 4);
    memcpy(h->fmt, "fmt ", 4);
    h->fmt_len = 16;
    h->format = 1;
    h->channels = 1;
    h->rate = rate;
    h->byte_rate = rate * 2;
    h->align = 2;
    h->bits = 16;
    memcpy(h->data, "data", 4);
    h->data_size = data_bytes;
}

bool voice_memo_wav_write_header(File* file, uint32_t sample_rate) {
    WavHeader h;
    wav_fill(&h, sample_rate, 0);
    return storage_file_write(file, &h, sizeof(h)) == sizeof(h);
}

bool voice_memo_wav_finalize(File* file, uint32_t sample_rate, uint32_t data_bytes) {
    WavHeader h;
    wav_fill(&h, sample_rate, data_bytes);
    if(!storage_file_seek(file, 0, true)) return false;
    return storage_file_write(file, &h, sizeof(h)) == sizeof(h);
}

static bool read_u32(File* file, uint32_t* out) {
    uint8_t b[4];
    if(storage_file_read(file, b, 4) != 4) return false;
    *out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
    return true;
}

static bool read_u16(File* file, uint16_t* out) {
    uint8_t b[2];
    if(storage_file_read(file, b, 2) != 2) return false;
    *out = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
    return true;
}

static bool skip_bytes(File* file, uint32_t n) {
    uint8_t tmp[64];
    while(n > 0) {
        uint32_t take = n > sizeof(tmp) ? sizeof(tmp) : n;
        if(storage_file_read(file, tmp, take) != take) return false;
        n -= take;
    }
    return true;
}

bool voice_memo_wav_read_header(
    File* file,
    uint32_t* sample_rate,
    uint16_t* channels,
    uint16_t* bits,
    uint32_t* data_bytes) {
    char id[4];
    if(storage_file_read(file, id, 4) != 4 || memcmp(id, "RIFF", 4) != 0) return false;
    uint32_t riff_size = 0;
    if(!read_u32(file, &riff_size)) return false;
    if(storage_file_read(file, id, 4) != 4 || memcmp(id, "WAVE", 4) != 0) return false;

    bool have_fmt = false;
    uint32_t rate = 0;
    uint16_t ch = 0, b = 0;
    /* Walk chunks (fmt/data + anything else) until the data chunk. */
    for(int guard = 0; guard < 16; guard++) {
        if(storage_file_read(file, id, 4) != 4) return false;
        uint32_t len = 0;
        if(!read_u32(file, &len)) return false;
        if(memcmp(id, "fmt ", 4) == 0) {
            uint16_t format = 0;
            uint32_t byte_rate = 0, align = 0;
            if(!read_u16(file, &format)) return false;
            if(!read_u16(file, &ch)) return false;
            if(!read_u32(file, &rate)) return false;
            if(!read_u32(file, &byte_rate)) return false;
            if(!read_u16(file, (uint16_t*)&align)) return false;
            if(!read_u16(file, &b)) return false;
            if(len > 16 && !skip_bytes(file, len - 16)) return false;
            if(format != 1) return false; /* PCM only */
            if(b != 8 && b != 16) return false;
            if(ch < 1 || ch > 2) return false;
            have_fmt = true;
        } else if(memcmp(id, "data", 4) == 0) {
            if(!have_fmt) return false;
            if(sample_rate) *sample_rate = rate;
            if(channels) *channels = ch;
            if(bits) *bits = b;
            if(data_bytes) *data_bytes = len;
            return true;
        } else {
            /* Unknown chunk (LIST, cue, ...): skip, padded to even size. */
            if(!skip_bytes(file, len + (len & 1))) return false;
        }
    }
    return false;
}

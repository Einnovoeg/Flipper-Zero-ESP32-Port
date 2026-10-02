#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default capture rate: 16 kHz 16-bit mono. Plenty for voice, cheap on CPU
 * and SD bandwidth (~32 KB/s). */
#define FURI_HAL_MIC_SAMPLE_RATE 16000

void furi_hal_mic_init(void);
void furi_hal_mic_deinit(void);

/** Exclusive ownership, same convention as furi_hal_speaker (the voice memo
 * app and any future listener must not fight over the I2S peripheral). */
bool furi_hal_mic_acquire(uint32_t timeout);
void furi_hal_mic_release(void);
bool furi_hal_mic_is_mine(void);

/** Open the PDM microphone at the given rate. Returns false on boards without
 * a microphone or if the peripheral cannot be started. */
bool furi_hal_mic_start(uint32_t sample_rate);

/** Stop capture and free the I2S channel. Safe to call when not started. */
void furi_hal_mic_stop(void);

/** Read up to `frames` mono int16 samples. Blocks up to timeout_ms.
 * Returns the number of frames actually read (0 on timeout / not started). */
size_t furi_hal_mic_read(int16_t* buf, size_t frames, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

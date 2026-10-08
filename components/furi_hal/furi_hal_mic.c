/**
 * @file furi_hal_mic.c
 * PDM microphone HAL — I2S PDM-RX capture for boards with microphone hardware,
 * no-op stubs for boards without (same structure as furi_hal_speaker.c).
 *
 * Currently only LilyGo T-Embed CC1101 has a mic (BOARD_HAS_MIC, PDM data on
 * BOARD_PIN_MIC_DATA, clock on BOARD_PIN_MIC_CLK). Capture runs on I2S_NUM_0
 * because the ESP32-S3 PDM RX mode is only supported on I2S0 (I2S1 has no PDM
 * receiver). The speaker also lives on I2S0 but uses Philips TX, and the two
 * paths are never active at the same time (record vs playback).
 */

#include "furi_hal_mic.h"
#include "boards/board.h"
#include <furi.h>

#define TAG "FuriHalMic"

#ifndef BOARD_HAS_MIC
#define BOARD_HAS_MIC 0
#endif

#if BOARD_HAS_MIC

#include <driver/i2s_pdm.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>

#define MIC_DMA_DESC_NUM  6
#define MIC_DMA_FRAME_NUM 256

static FuriMutex* mic_mutex = NULL;
static i2s_chan_handle_t mic_rx_handle = NULL;
static bool mic_started = false;
static uint32_t mic_owner = 0;

void furi_hal_mic_init(void) {
    if(!mic_mutex) mic_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
}

void furi_hal_mic_deinit(void) {
    furi_hal_mic_stop();
    if(mic_mutex) {
        furi_mutex_free(mic_mutex);
        mic_mutex = NULL;
    }
    mic_owner = 0;
}

bool furi_hal_mic_acquire(uint32_t timeout) {
    if(!mic_mutex) return false;
    if(furi_mutex_acquire(mic_mutex, timeout) != FuriStatusOk) return false;
    mic_owner = (uint32_t)furi_thread_get_current_id();
    return true;
}

void furi_hal_mic_release(void) {
    mic_owner = 0;
    if(mic_mutex) furi_mutex_release(mic_mutex);
}

bool furi_hal_mic_is_mine(void) {
    return mic_mutex && (mic_owner == (uint32_t)furi_thread_get_current_id());
}

bool furi_hal_mic_start(uint32_t sample_rate) {
    if(mic_rx_handle) return true;
    if(sample_rate == 0) sample_rate = FURI_HAL_MIC_SAMPLE_RATE;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = MIC_DMA_DESC_NUM;
    chan_cfg.dma_frame_num = MIC_DMA_FRAME_NUM;
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &mic_rx_handle);
    if(err != ESP_OK) {
        FURI_LOG_E(TAG, "i2s_new_channel failed: %s (0x%x)", esp_err_to_name(err), err);
        mic_rx_handle = NULL;
        return false;
    }

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = (gpio_num_t)BOARD_PIN_MIC_CLK,
            .din = (gpio_num_t)BOARD_PIN_MIC_DATA,
        },
    };
    err = i2s_channel_init_pdm_rx_mode(mic_rx_handle, &pdm_cfg);
    if(err != ESP_OK) {
        FURI_LOG_E(TAG, "pdm_rx_mode init failed: %s (0x%x)", esp_err_to_name(err), err);
        i2s_del_channel(mic_rx_handle);
        mic_rx_handle = NULL;
        return false;
    }
    err = i2s_channel_enable(mic_rx_handle);
    if(err != ESP_OK) {
        FURI_LOG_E(TAG, "channel enable failed: %s (0x%x)", esp_err_to_name(err), err);
        i2s_del_channel(mic_rx_handle);
        mic_rx_handle = NULL;
        return false;
    }
    mic_started = true;
    FURI_LOG_I(TAG, "capture started @%lu Hz", (unsigned long)sample_rate);
    return true;
}

void furi_hal_mic_stop(void) {
    if(mic_rx_handle) {
        i2s_channel_disable(mic_rx_handle);
        i2s_del_channel(mic_rx_handle);
        mic_rx_handle = NULL;
    }
    mic_started = false;
}

size_t furi_hal_mic_read(int16_t* buf, size_t frames, uint32_t timeout_ms) {
    if(!buf || frames == 0 || !mic_rx_handle || !mic_started) return 0;
    size_t bytes_to_read = frames * sizeof(int16_t);
    size_t bytes_read = 0;
    esp_err_t err =
        i2s_channel_read(mic_rx_handle, (void*)buf, bytes_to_read, &bytes_read, timeout_ms);
    /* On ESP_ERR_TIMEOUT the driver has still filled part of the buffer with
     * captured audio. Keep it — dropping partials here loses samples and makes
     * the wall-clock-measured capture rate come out too low. */
    if(bytes_read == 0 && err != ESP_OK) {
        FURI_LOG_W(TAG, "mic read: %s (0x%x)", esp_err_to_name(err), err);
        return 0;
    }
    return bytes_read / sizeof(int16_t);
}

#else /* !BOARD_HAS_MIC */

void furi_hal_mic_init(void) {
}

void furi_hal_mic_deinit(void) {
}

bool furi_hal_mic_acquire(uint32_t timeout) {
    (void)timeout;
    return false;
}

void furi_hal_mic_release(void) {
}

bool furi_hal_mic_is_mine(void) {
    return false;
}

bool furi_hal_mic_start(uint32_t sample_rate) {
    (void)sample_rate;
    return false;
}

void furi_hal_mic_stop(void) {
}

size_t furi_hal_mic_read(int16_t* buf, size_t frames, uint32_t timeout_ms) {
    (void)buf;
    (void)frames;
    (void)timeout_ms;
    return 0;
}

#endif /* BOARD_HAS_MIC */

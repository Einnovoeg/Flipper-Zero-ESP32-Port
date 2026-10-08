#include "voice_memo.h"
#include "voice_memo_view.h"
#include "voice_memo_wav.h"

#ifndef BOARD_HAS_MIC
#define BOARD_HAS_MIC 0
#endif

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <storage/storage.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "../streaming/mp3_i2s.h"

#define TAG "VoiceMemo"

#define VOICE_MEMO_SAMPLE_RATE 16000
#define VOICE_MEMO_CHUNK_FRAMES 512
#define VOICE_MEMO_STAGE_FRAMES 8192 /* 16 KB = 0.5 s @ 16 kHz before an SD write */
#define VOICE_MEMO_WORKER_STACK 4096
/* Record gain: mic peaks measured ~-6 dBFS (16227/32768), so x2 lands peaks
 * at full scale with no practical clipping of speech. Raise to 3 only if the
 * logged peak stays below ~11000. */
#define VOICE_MEMO_RECORD_GAIN 2

/* Submenu custom events: 0x100 + item index (0 = record new). */
#define VOICE_MEMO_EV_ITEM_BASE 0x100
#define VOICE_MEMO_EV_STOP      0x001

static int file_name_cmp(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

static void voice_memo_submenu_callback(void* context, uint32_t index);

/* Rebuild the submenu from the voice_memos dir (.wav takes). */
static void voice_memo_rescan(VoiceMemoApp* app, const char* status) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, app->dir_path);

    app->file_count = 0;
    File* dir = storage_file_alloc(storage);
    if(storage_dir_open(dir, app->dir_path)) {
        FileInfo info;
        char name[VOICE_MEMO_NAME_LEN];
        while(app->file_count < VOICE_MEMO_MAX_FILES &&
              storage_dir_read(dir, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info)) continue;
            size_t len = strlen(name);
            if(len < 5) continue;
            if(strcasecmp(name + len - 4, ".wav") != 0) continue;
            strncpy(
                app->file_names[app->file_count], name, VOICE_MEMO_NAME_LEN - 1);
            app->file_names[app->file_count][VOICE_MEMO_NAME_LEN - 1] = '\0';
            app->file_count++;
        }
        storage_dir_close(dir);
    }
    storage_file_free(dir);
    furi_record_close(RECORD_STORAGE);

    qsort(
        app->file_names, app->file_count, VOICE_MEMO_NAME_LEN, file_name_cmp);

    submenu_reset(app->submenu);
    if(status && status[0]) {
        strncpy(app->header, status, sizeof(app->header) - 1);
        app->header[sizeof(app->header) - 1] = '\0';
    } else if(app->file_count == 0) {
        snprintf(app->header, sizeof(app->header), "No memos yet");
    } else {
        snprintf(
            app->header,
            sizeof(app->header),
            "%u memo%s",
            (unsigned)app->file_count,
            app->file_count == 1 ? "" : "s");
    }
    submenu_set_header(app->submenu, app->header);
    submenu_add_item(
        app->submenu, "Record new", 0, voice_memo_submenu_callback, app);
    for(uint8_t i = 0; i < app->file_count; i++) {
        submenu_add_item(
            app->submenu, app->file_names[i], i + 1, voice_memo_submenu_callback, app);
    }
}

/* First free memo_NNN.wav name. Returns false when the bank is full. */
static bool voice_memo_next_name(VoiceMemoApp* app, char* out, size_t outsz) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool found = false;
    for(int n = 1; n <= 999; n++) {
        char base[VOICE_MEMO_NAME_LEN];
        snprintf(base, sizeof(base), "memo_%03d.wav", n);
        char full[VOICE_MEMO_PATH_LEN];
        full[0] = '\0';
        strncat(full, app->dir_path, sizeof(full) - 1);
        strncat(full, "/", sizeof(full) - strlen(full) - 1);
        strncat(full, base, sizeof(full) - strlen(full) - 1);
        FileInfo info;
        if(storage_common_stat(storage, full, &info) != FSE_OK) {
            strncpy(out, base, outsz - 1);
            out[outsz - 1] = '\0';
            found = true;
            break;
        }
    }
    furi_record_close(RECORD_STORAGE);
    return found;
}

/* ---- worker threads ---- */

static int32_t voice_memo_record_thread(void* ctx) {
    VoiceMemoApp* app = ctx;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = false;

    if(!furi_hal_mic_acquire(furi_ms_to_ticks(500))) {
        FURI_LOG_E(TAG, "mic busy");
    } else if(!furi_hal_mic_start(VOICE_MEMO_SAMPLE_RATE)) {
        FURI_LOG_E(TAG, "mic start failed (no microphone?)");
        furi_hal_mic_release();
    } else {
        File* f = storage_file_alloc(storage);
        if(!storage_file_open(f, app->file_path, FSAM_WRITE, FSOM_CREATE_ALWAYS) ||
           !voice_memo_wav_write_header(f, VOICE_MEMO_SAMPLE_RATE)) {
            FURI_LOG_E(TAG, "cannot create %s", app->file_path);
        } else {
            static int16_t chunk[VOICE_MEMO_CHUNK_FRAMES];
            /* Stage 0.5 s of audio before touching the SD card. Writing every
             * 1 KB stalled the loop ~60 ms per call, the mic DMA ring
             * overflowed, and the take came out subsampled (slow playback).
             * One 16 KB write per ~0.5 s keeps the loop ahead of the DMA. */
            static int16_t stage[VOICE_MEMO_STAGE_FRAMES];
            size_t stage_n = 0;
            uint32_t frames = 0;
            uint32_t max_peak = 0;
            uint32_t start_tick = furi_get_tick();
            uint32_t log_tick = start_tick;
            uint32_t view_tick = start_tick;
            uint32_t win_frames = 0;
            uint32_t max_read_ms = 0, max_write_ms = 0;
            ok = true;
            while(app->worker_run) {
                uint32_t t0 = furi_get_tick();
                size_t got = furi_hal_mic_read(
                    chunk, VOICE_MEMO_CHUNK_FRAMES, 200);
                uint32_t read_ms = furi_get_tick() - t0;
                if(read_ms > max_read_ms) max_read_ms = read_ms;
                if(got == 0) continue;
                /* Level = peak of the RAW chunk (input diagnostic), then apply
                 * the record gain before the samples hit the SD card. */
                int peak = 0;
                for(size_t i = 0; i < got; i++) {
                    int v = chunk[i] < 0 ? -chunk[i] : chunk[i];
                    if(v > peak) peak = v;
                    int32_t g = (int32_t)chunk[i] * VOICE_MEMO_RECORD_GAIN;
                    if(g > 32767) g = 32767;
                    if(g < -32768) g = -32768;
                    chunk[i] = (int16_t)g;
                }
                /* Flush before appending so the staging buffer can never
                 * overrun (got varies with partial reads). */
                if(stage_n + got > VOICE_MEMO_STAGE_FRAMES) {
                    uint32_t tw0 = furi_get_tick();
                    size_t nbytes = stage_n * sizeof(int16_t);
                    if(storage_file_write(f, stage, nbytes) != nbytes) {
                        FURI_LOG_E(TAG, "SD write failed");
                        ok = false;
                        break;
                    }
                    uint32_t write_ms = furi_get_tick() - tw0;
                    if(write_ms > max_write_ms) max_write_ms = write_ms;
                    stage_n = 0;
                }
                memcpy(&stage[stage_n], chunk, got * sizeof(int16_t));
                stage_n += got;
                if((uint32_t)peak > max_peak) max_peak = (uint32_t)peak;
                frames += got;
                win_frames += got;
                uint32_t now = furi_get_tick();
                if(now - log_tick >= 1000) {
                    uint32_t rate =
                        (uint32_t)(((uint64_t)win_frames * 1000) / (now - log_tick));
                    FURI_LOG_I(
                        TAG,
                        "rec %lu Hz want %d (read_max %lu ms, write_max %lu ms)",
                        (unsigned long)rate,
                        VOICE_MEMO_SAMPLE_RATE,
                        (unsigned long)max_read_ms,
                        (unsigned long)max_write_ms);
                    log_tick = now;
                    win_frames = 0;
                    max_read_ms = max_write_ms = 0;
                }
                if(now - view_tick >= 250) {
                    /* Redraw at 4 Hz: the full-screen repaint took CPU time
                     * that the capture loop could not spare. */
                    voice_memo_view_set_recording(
                        app->vm_view,
                        frames / VOICE_MEMO_SAMPLE_RATE,
                        (uint8_t)((peak * 100) / 32768));
                    view_tick = now;
                }
            }
            if(ok && stage_n > 0) {
                size_t nbytes = stage_n * sizeof(int16_t);
                if(storage_file_write(f, stage, nbytes) != nbytes) {
                    FURI_LOG_E(TAG, "SD write failed (flush)");
                    ok = false;
                }
            }
            uint32_t data_bytes = frames * sizeof(int16_t);
            /* The PDM clock does not always land exactly on the requested
             * rate; measure the true samples/sec we captured and stamp that
             * into the WAV header so playback runs at the right speed. */
            uint32_t elapsed_ms = furi_get_tick() - start_tick;
            uint32_t actual_rate = VOICE_MEMO_SAMPLE_RATE;
            if(elapsed_ms > 0 && frames > 0) {
                uint32_t measured = (uint32_t)(((uint64_t)frames * 1000) / elapsed_ms);
                if(measured >= 8000 && measured <= 48000) actual_rate = measured;
            }
            FURI_LOG_I(
                TAG,
                "capture: %lu frames in %lu ms -> %lu Hz, peak=%lu/32768",
                (unsigned long)frames,
                (unsigned long)elapsed_ms,
                (unsigned long)actual_rate,
                (unsigned long)max_peak);
            if(!voice_memo_wav_finalize(f, actual_rate, data_bytes)) {
                FURI_LOG_E(TAG, "cannot finalize wav");
                ok = false;
            }
            storage_file_close(f);
            FURI_LOG_I(TAG, "saved %s (%lu frames)", app->file_path, (unsigned long)frames);
        }
        storage_file_free(f);
        furi_hal_mic_stop();
        furi_hal_mic_release();
    }

    furi_record_close(RECORD_STORAGE);
    if(!ok) {
        /* Drop empty/failed takes so the list never shows junk. */
        Storage* st = furi_record_open(RECORD_STORAGE);
        File* f = storage_file_alloc(st);
        storage_common_remove(st, app->file_path);
        storage_file_free(f);
        furi_record_close(RECORD_STORAGE);
    }
    view_dispatcher_send_custom_event(app->dispatcher, VOICE_MEMO_EV_STOP);
    return 0;
}

static int32_t voice_memo_play_thread(void* ctx) {
    VoiceMemoApp* app = ctx;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    uint32_t rate = 0, data_bytes = 0;
    uint16_t ch = 0, bits = 0;
    bool opened = storage_file_open(f, app->file_path, FSAM_READ, FSOM_OPEN_EXISTING) &&
                  voice_memo_wav_read_header(f, &rate, &ch, &bits, &data_bytes);
    if(!opened) {
        FURI_LOG_E(TAG, "cannot open %s", app->file_path);
        storage_file_close(f);
        storage_file_free(f);
        furi_record_close(RECORD_STORAGE);
        view_dispatcher_send_custom_event(app->dispatcher, VOICE_MEMO_EV_STOP);
        return 0;
    }

    uint32_t total_frames = data_bytes / (ch * bits / 8);
    uint32_t total_sec = rate ? total_frames / rate : 0;
    mp3_i2s_set_volume(150);
    if(!mp3_i2s_init(rate ? rate : VOICE_MEMO_SAMPLE_RATE)) {
        FURI_LOG_E(TAG, "speaker busy");
        storage_file_close(f);
        storage_file_free(f);
        furi_record_close(RECORD_STORAGE);
        view_dispatcher_send_custom_event(app->dispatcher, VOICE_MEMO_EV_STOP);
        return 0;
    }

    static int16_t mono[1024];
    static int16_t stereo[2048];
    size_t frame_bytes = ch * bits / 8;
    uint32_t played = 0;
    while(app->worker_run) {
        size_t want = 1024;
        if(played + want > total_frames) want = total_frames - played;
        if(want == 0) break;
        size_t got_bytes = storage_file_read(f, mono, want * frame_bytes);
        size_t got = got_bytes / frame_bytes;
        if(got == 0) break;
        if(ch == 1 && bits == 16) {
            for(size_t i = 0; i < got; i++) {
                stereo[i * 2 + 0] = mono[i];
                stereo[i * 2 + 1] = mono[i];
            }
        } else if(ch == 2 && bits == 16) {
            memcpy(stereo, mono, got * 2 * sizeof(int16_t));
        } else if(bits == 8) {
            for(size_t i = 0; i < got; i++) {
                int16_t s = (int16_t)(((uint8_t*)mono)[i * ch] - 128) * 256;
                stereo[i * 2 + 0] = s;
                stereo[i * 2 + 1] = (ch == 2) ?
                    (int16_t)(((uint8_t*)mono)[i * ch + 1] - 128) * 256 :
                    s;
            }
        } else {
            break;
        }
        mp3_i2s_push(stereo, got, 200);
        played += got;
        voice_memo_view_set_playback(
            app->vm_view,
            rate ? played / rate : 0,
            total_sec,
            total_frames ? (uint16_t)(played * 100 / total_frames) : 100);
    }

    mp3_i2s_flush();
    mp3_i2s_deinit();
    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    view_dispatcher_send_custom_event(app->dispatcher, VOICE_MEMO_EV_STOP);
    return 0;
}

/* ---- app plumbing ---- */

static void voice_memo_stop_worker(VoiceMemoApp* app) {
    if(app->worker) {
        app->worker_run = false;
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
    voice_memo_view_set_stopped(app->vm_view);
}

static void voice_memo_start_record(VoiceMemoApp* app) {
    char base[VOICE_MEMO_NAME_LEN];
    if(!voice_memo_next_name(app, base, sizeof(base))) {
        voice_memo_rescan(app, "Bank full (999 memos)");
        return;
    }
    app->file_path[0] = '\0';
    strncat(app->file_path, app->dir_path, sizeof(app->file_path) - 1);
    strncat(app->file_path, "/", sizeof(app->file_path) - strlen(app->file_path) - 1);
    strncat(app->file_path, base, sizeof(app->file_path) - strlen(app->file_path) - 1);
    voice_memo_view_set_mode(app->vm_view, VoiceMemoViewModeRecord);
    voice_memo_view_set_file(app->vm_view, base);
    voice_memo_view_set_recording(app->vm_view, 0, 0);
    view_dispatcher_switch_to_view(app->dispatcher, VoiceMemoViewRecord);
    app->worker_run = true;
    app->worker = furi_thread_alloc_ex(
        "VoiceMemoRec", VOICE_MEMO_WORKER_STACK, voice_memo_record_thread, app);
    furi_thread_start(app->worker);
}

static void voice_memo_start_play(VoiceMemoApp* app, uint8_t file_index) {
    if(file_index >= app->file_count) return;
    app->file_path[0] = '\0';
    strncat(app->file_path, app->dir_path, sizeof(app->file_path) - 1);
    strncat(app->file_path, "/", sizeof(app->file_path) - strlen(app->file_path) - 1);
    strncat(
        app->file_path,
        app->file_names[file_index],
        sizeof(app->file_path) - strlen(app->file_path) - 1);
    voice_memo_view_set_mode(app->vm_view, VoiceMemoViewModePlay);
    voice_memo_view_set_file(app->vm_view, app->file_names[file_index]);
    voice_memo_view_set_playback(app->vm_view, 0, 0, 0);
    view_dispatcher_switch_to_view(app->dispatcher, VoiceMemoViewRecord);
    app->worker_run = true;
    app->worker = furi_thread_alloc_ex(
        "VoiceMemoPlay", VOICE_MEMO_WORKER_STACK, voice_memo_play_thread, app);
    furi_thread_start(app->worker);
}

static void voice_memo_submenu_callback(void* context, uint32_t index) {
    VoiceMemoApp* app = context;
    view_dispatcher_send_custom_event(
        app->dispatcher, VOICE_MEMO_EV_ITEM_BASE + index);
}

static void voice_memo_stop_callback(void* context) {
    VoiceMemoApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, VOICE_MEMO_EV_STOP);
}

static bool voice_memo_custom_event_callback(void* context, uint32_t event) {
    VoiceMemoApp* app = context;
    if(event == VOICE_MEMO_EV_STOP) {
        /* Worker ended (user stop, error, or playback finished). */
        voice_memo_stop_worker(app);
        voice_memo_rescan(app, NULL);
        view_dispatcher_switch_to_view(app->dispatcher, VoiceMemoViewSubmenu);
        return true;
    }
    if(event >= VOICE_MEMO_EV_ITEM_BASE) {
        uint32_t index = event - VOICE_MEMO_EV_ITEM_BASE;
        if(index == 0) {
            voice_memo_start_record(app);
        } else {
            voice_memo_start_play(app, (uint8_t)(index - 1));
        }
        return true;
    }
    return false;
}

static bool voice_memo_back_event_callback(void* context) {
    VoiceMemoApp* app = context;
    /* Stop any running worker (finalizing a partial take) before leaving,
     * so hold-Back-to-home never strands a thread on freed memory. */
    voice_memo_stop_worker(app);
    return false; /* not consumed -> dispatcher stops, back to desktop */
}

static VoiceMemoApp* voice_memo_alloc(void) {
    VoiceMemoApp* app = malloc(sizeof(VoiceMemoApp));
    memset(app, 0, sizeof(VoiceMemoApp));

    snprintf(app->dir_path, sizeof(app->dir_path), "%s", EXT_PATH(VOICE_MEMO_DIR_NAME));

    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->vm_view = voice_memo_view_alloc();

    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->dispatcher, voice_memo_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->dispatcher, voice_memo_back_event_callback);

    voice_memo_view_set_stop_callback(app->vm_view, voice_memo_stop_callback, app);

    view_dispatcher_add_view(
        app->dispatcher, VoiceMemoViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->dispatcher, VoiceMemoViewRecord, voice_memo_view_get_view(app->vm_view));

    voice_memo_rescan(app, NULL);

    /* Select the initial view: without this the dispatcher runs viewless
     * (app invisible, input lost, loader stays locked forever). */
    view_dispatcher_switch_to_view(app->dispatcher, VoiceMemoViewSubmenu);

    view_dispatcher_attach_to_gui(
        app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void voice_memo_free(VoiceMemoApp* app) {
    voice_memo_stop_worker(app);
    view_dispatcher_remove_view(app->dispatcher, VoiceMemoViewSubmenu);
    view_dispatcher_remove_view(app->dispatcher, VoiceMemoViewRecord);
    voice_memo_view_free(app->vm_view);
    submenu_free(app->submenu);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t voice_memo_app(void* p) {
    UNUSED(p);
    FURI_LOG_I("VoiceMemo", "starting");

    VoiceMemoApp* app = voice_memo_alloc();
#if !BOARD_HAS_MIC
    voice_memo_rescan(app, "No microphone on this board");
#endif
    view_dispatcher_run(app->dispatcher);
    voice_memo_free(app);

    FURI_LOG_I("VoiceMemo", "exit");
    return 0;
}

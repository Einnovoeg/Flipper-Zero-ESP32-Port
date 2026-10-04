#pragma once

#include <furi.h>
#include <gui/view.h>

typedef enum {
    VoiceMemoViewModeRecord,
    VoiceMemoViewModePlay,
} VoiceMemoViewMode;

typedef struct VoiceMemoRecordView VoiceMemoRecordView;

typedef void (*VoiceMemoStopCallback)(void* context);

/* Single view for both modes: recording (live level + timer) and playback
 * (progress + timer). The worker thread pushes state through the setters;
 * stop (Ok/Back short) is reported via the stop callback. */
VoiceMemoRecordView* voice_memo_view_alloc(void);
void voice_memo_view_free(VoiceMemoRecordView* view);
View* voice_memo_view_get_view(VoiceMemoRecordView* view);

void voice_memo_view_set_mode(VoiceMemoRecordView* view, VoiceMemoViewMode mode);
void voice_memo_view_set_file(VoiceMemoRecordView* view, const char* name);
void voice_memo_view_set_recording(
    VoiceMemoRecordView* view,
    uint32_t elapsed_sec,
    uint8_t level_pct);
void voice_memo_view_set_playback(
    VoiceMemoRecordView* view,
    uint32_t elapsed_sec,
    uint32_t total_sec,
    uint16_t progress_pct);
void voice_memo_view_set_stopped(VoiceMemoRecordView* view);
void voice_memo_view_set_stop_callback(
    VoiceMemoRecordView* view,
    VoiceMemoStopCallback callback,
    void* context);

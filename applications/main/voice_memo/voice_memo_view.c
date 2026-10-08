#include "voice_memo_view.h"

#include <gui/canvas.h>
#include <gui/elements.h>
#include <input/input.h>
#include <string.h>
#include <stdio.h>
#include "../streaming/mp3_i2s.h"

struct VoiceMemoRecordView {
    View* view;
    VoiceMemoStopCallback stop_cb;
    void* stop_ctx;
};

typedef struct {
    VoiceMemoViewMode mode;
    char file[32];
    uint32_t elapsed_sec;
    uint32_t total_sec;
    uint8_t pct; /* level (record) or progress (play) */
    bool stopped;
    uint8_t volume; /* playback volume 0..100 */
} VoiceMemoModel;

static void voice_memo_draw(Canvas* canvas, void* ctx) {
    VoiceMemoModel* m = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    if(m->mode == VoiceMemoViewModeRecord) {
        canvas_draw_str(canvas, 2, 10, m->stopped ? "Stopped" : "REC ●");
    } else {
        canvas_draw_str(canvas, 2, 10, m->stopped ? "Done" : "Playing");
    }
    canvas_draw_str(canvas, 44, 10, m->file);

    /* Timer mm:ss (/ total for playback). Bounded to two digits so the
     * format always fits (memos are short takes by design). */
    char t[16];
    unsigned em = (unsigned)((m->elapsed_sec / 60) % 100);
    unsigned es = (unsigned)(m->elapsed_sec % 60);
    if(m->mode == VoiceMemoViewModePlay && m->total_sec > 0) {
        unsigned tm = (unsigned)((m->total_sec / 60) % 100);
        unsigned ts = (unsigned)(m->total_sec % 60);
        snprintf(t, sizeof(t), "%02u:%02u/%02u:%02u", em, es, tm, ts);
    } else {
        snprintf(t, sizeof(t), "%02u:%02u", em, es);
    }
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 26, t);

    /* Bar: input level while recording, progress while playing. */
    canvas_draw_frame(canvas, 2, 32, 124, 10);
    uint8_t w = m->pct > 100 ? 100 : m->pct;
    if(w > 0) canvas_draw_box(canvas, 4, 34, (w * 120) / 100, 6);

    canvas_set_font(canvas, FontSecondary);
    if(m->mode == VoiceMemoViewModeRecord) {
        canvas_draw_str(canvas, 2, 54, m->stopped ? "Ok: back" : "Ok/Back: stop");
    } else {
        canvas_draw_str(canvas, 2, 54, m->stopped ? "Ok: back" : "Ok/Back: stop");
        if(!m->stopped) {
            char vol[10];
            snprintf(vol, sizeof(vol), "V%3u", (unsigned)m->volume);
            canvas_draw_str(canvas, 104, 54, vol);
        }
    }
    canvas_draw_str(canvas, 2, 63, "Hold Back: home");
}

static bool voice_memo_input(InputEvent* event, void* ctx) {
    VoiceMemoRecordView* view = ctx;

    /* Volume up/down while a memo is playing. */
    if((event->type == InputTypeShort || event->type == InputTypeRepeat) &&
       (event->key == InputKeyUp || event->key == InputKeyDown)) {
        bool handled = false;
        with_view_model(
            view->view,
            VoiceMemoModel * m,
            {
                if(m->mode == VoiceMemoViewModePlay && !m->stopped) {
                    /* Knob feel, not menu feel: clockwise (Down) turns UP. */
                    int v = (int)m->volume + (event->key == InputKeyDown ? +10 : -10);
                    if(v < 0) v = 0;
                    if(v > 100) v = 100;
                    m->volume = (uint8_t)v;
                    mp3_i2s_set_volume(m->volume);
                    handled = true;
                }
            },
            true);
        if(handled) return true;
    }

    if(!view->stop_cb) return false;
    if((event->type == InputTypeShort) &&
       (event->key == InputKeyOk || event->key == InputKeyBack)) {
        view->stop_cb(view->stop_ctx);
        return true;
    }
    return false;
}

VoiceMemoRecordView* voice_memo_view_alloc(void) {
    VoiceMemoRecordView* view = malloc(sizeof(VoiceMemoRecordView));
    view->view = view_alloc();
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, voice_memo_draw);
    view_set_input_callback(view->view, voice_memo_input);
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(VoiceMemoModel));
    with_view_model(
        view->view,
        VoiceMemoModel * m,
        {
            memset(m, 0, sizeof(*m));
            m->mode = VoiceMemoViewModeRecord;
            m->volume = 100;
        },
        false);
    view->stop_cb = NULL;
    view->stop_ctx = NULL;
    return view;
}

void voice_memo_view_free(VoiceMemoRecordView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* voice_memo_view_get_view(VoiceMemoRecordView* view) {
    furi_assert(view);
    return view->view;
}

void voice_memo_view_set_mode(VoiceMemoRecordView* view, VoiceMemoViewMode mode) {
    with_view_model(
        view->view,
        VoiceMemoModel * m,
        {
            m->mode = mode;
            m->stopped = false;
            if(mode == VoiceMemoViewModePlay) m->volume = 100;
        },
        true);
}

void voice_memo_view_set_file(VoiceMemoRecordView* view, const char* name) {
    with_view_model(
        view->view,
        VoiceMemoModel * m,
        {
            strncpy(m->file, name ? name : "", sizeof(m->file) - 1);
            m->file[sizeof(m->file) - 1] = '\0';
        },
        true);
}

void voice_memo_view_set_recording(
    VoiceMemoRecordView* view,
    uint32_t elapsed_sec,
    uint8_t level_pct) {
    with_view_model(
        view->view,
        VoiceMemoModel * m,
        {
            m->elapsed_sec = elapsed_sec;
            m->pct = level_pct;
        },
        true);
}

void voice_memo_view_set_playback(
    VoiceMemoRecordView* view,
    uint32_t elapsed_sec,
    uint32_t total_sec,
    uint16_t progress_pct) {
    with_view_model(
        view->view,
        VoiceMemoModel * m,
        {
            m->elapsed_sec = elapsed_sec;
            m->total_sec = total_sec;
            m->pct = progress_pct > 100 ? 100 : (uint8_t)progress_pct;
        },
        true);
}

void voice_memo_view_set_stopped(VoiceMemoRecordView* view) {
    with_view_model(view->view, VoiceMemoModel * m, { m->stopped = true; }, true);
}

void voice_memo_view_set_stop_callback(
    VoiceMemoRecordView* view,
    VoiceMemoStopCallback callback,
    void* context) {
    view->stop_cb = callback;
    view->stop_ctx = context;
}

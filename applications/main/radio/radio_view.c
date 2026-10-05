#include "radio_view.h"

#include <gui/canvas.h>
#include <input/input.h>
#include <string.h>
#include <stdio.h>

/* Keep in sync with radio.h. Defined here (not included) so this translation
 * unit has no dependency on the app header. */
#define RADIO_NAME_LEN  32
#define RADIO_TITLE_LEN 64

struct RadioPlayerView {
    View* view;
    RadioPlayerStopCallback stop_cb;
    void* stop_ctx;
    RadioPlayerVolCallback vol_cb;
    void* vol_ctx;
};

typedef struct {
    char station[RADIO_NAME_LEN];
    char track[RADIO_TITLE_LEN];
    uint32_t elapsed_sec;
    uint8_t volume;
    char state[24];
} RadioModel;

static void radio_draw(Canvas* canvas, void* ctx) {
    RadioModel* m = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 10, m->station);
    canvas_set_font(canvas, FontPrimary);
    /* Track title, truncated to fit. */
    char title[22];
    strncpy(title, m->track[0] ? m->track : "(no metadata)", sizeof(title) - 1);
    title[sizeof(title) - 1] = '\0';
    canvas_draw_str(canvas, 2, 26, title);

    char t[16];
    snprintf(
        t, sizeof(t), "%02u:%02u  Vol %u", (unsigned)((m->elapsed_sec / 60) % 100),
        (unsigned)(m->elapsed_sec % 60), (unsigned)m->volume);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 40, t);
    canvas_draw_str(canvas, 2, 52, m->state);
    canvas_draw_str(canvas, 2, 63, "Up/Dn vol  Ok: stop");
}

static bool radio_input(InputEvent* event, void* ctx) {
    RadioPlayerView* view = ctx;
    if(event->type != InputTypeShort) return false;
    if(event->key == InputKeyOk || event->key == InputKeyBack) {
        if(view->stop_cb) view->stop_cb(view->stop_ctx);
        return true;
    }
    if(event->key == InputKeyUp || event->key == InputKeyDown) {
        if(view->vol_cb) {
            view->vol_cb(view->vol_ctx, event->key == InputKeyUp ? 5 : -5);
        }
        return true;
    }
    return false;
}

RadioPlayerView* radio_view_alloc(void) {
    RadioPlayerView* view = malloc(sizeof(RadioPlayerView));
    view->view = view_alloc();
    view_set_context(view->view, view);
    view_set_draw_callback(view->view, radio_draw);
    view_set_input_callback(view->view, radio_input);
    view_allocate_model(view->view, ViewModelTypeLocking, sizeof(RadioModel));
    with_view_model(
        view->view, RadioModel * m, { memset(m, 0, sizeof(*m)); }, false);
    view->stop_cb = NULL;
    view->stop_ctx = NULL;
    view->vol_cb = NULL;
    view->vol_ctx = NULL;
    return view;
}

void radio_view_free(RadioPlayerView* view) {
    furi_assert(view);
    view_free(view->view);
    free(view);
}

View* radio_view_get_view(RadioPlayerView* view) {
    furi_assert(view);
    return view->view;
}

void radio_view_set_station(RadioPlayerView* view, const char* station) {
    with_view_model(
        view->view,
        RadioModel * m,
        {
            strncpy(m->station, station ? station : "", sizeof(m->station) - 1);
            m->station[sizeof(m->station) - 1] = '\0';
        },
        true);
}

void radio_view_set_track(RadioPlayerView* view, const char* title) {
    with_view_model(
        view->view,
        RadioModel * m,
        {
            strncpy(m->track, title ? title : "", sizeof(m->track) - 1);
            m->track[sizeof(m->track) - 1] = '\0';
        },
        true);
}

void radio_view_set_status(
    RadioPlayerView* view,
    uint32_t elapsed_sec,
    uint8_t volume,
    const char* state) {
    with_view_model(
        view->view,
        RadioModel * m,
        {
            m->elapsed_sec = elapsed_sec;
            m->volume = volume;
            strncpy(m->state, state ? state : "", sizeof(m->state) - 1);
            m->state[sizeof(m->state) - 1] = '\0';
        },
        true);
}

void radio_view_set_stop_callback(
    RadioPlayerView* view,
    RadioPlayerStopCallback callback,
    void* context) {
    view->stop_cb = callback;
    view->stop_ctx = context;
}

void radio_view_set_vol_callback(
    RadioPlayerView* view,
    RadioPlayerVolCallback callback,
    void* context) {
    view->vol_cb = callback;
    view->vol_ctx = context;
}

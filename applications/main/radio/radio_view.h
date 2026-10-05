#pragma once

#include <furi.h>
#include <gui/view.h>

typedef struct RadioPlayerView RadioPlayerView;

typedef void (*RadioPlayerStopCallback)(void* context);
typedef void (*RadioPlayerVolCallback)(void* context, int8_t delta);

RadioPlayerView* radio_view_alloc(void);
void radio_view_free(RadioPlayerView* view);
View* radio_view_get_view(RadioPlayerView* view);

/* station/title shown; elapsed_sec ticks while playing; volume 0..100;
 * state text is a short status line ("Buffering...", "Playing", "Stopped"). */
void radio_view_set_station(RadioPlayerView* view, const char* station);
void radio_view_set_track(RadioPlayerView* view, const char* title);
void radio_view_set_status(
    RadioPlayerView* view,
    uint32_t elapsed_sec,
    uint8_t volume,
    const char* state);
void radio_view_set_stop_callback(
    RadioPlayerView* view,
    RadioPlayerStopCallback callback,
    void* context);
void radio_view_set_vol_callback(
    RadioPlayerView* view,
    RadioPlayerVolCallback callback,
    void* context);

#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>

#include "radio_view.h"

#define RADIO_MAX_STATIONS  16
#define RADIO_URL_LEN       160
#define RADIO_NAME_LEN      32
#define RADIO_TITLE_LEN     64

typedef enum {
    RadioViewSubmenu,
    RadioViewInput,
    RadioViewPlayer,
    RadioViewCount,
} RadioView;

/* Submenu custom events. */
typedef enum {
    RadioEventCustomURL = 200,
    RadioEventStationBase = 300, /* + station index */
    RadioEventStop = 400,
} RadioEvent;

typedef struct {
    char name[RADIO_NAME_LEN];
    char url[RADIO_URL_LEN];
} RadioStation;

typedef struct RadioApp {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* submenu;
    TextInput* text_input;
    char text_buf[RADIO_URL_LEN];
    RadioPlayerView* player_view;
    FuriThread* worker;
    volatile bool worker_run;
    RadioStation stations[RADIO_MAX_STATIONS];
    uint8_t station_count;
    char header[48];
    char status[48];
    uint8_t volume;
} RadioApp;

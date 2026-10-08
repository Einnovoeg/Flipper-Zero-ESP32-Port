#include "radio.h"
#include "radio_view.h"
#include "radio_stream.h"
#include "../streaming/mp3_sink.h"

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <storage/storage.h>
#include <wifi.h>
#include <string.h>
#include <stdio.h>

#define TAG "Radio"

/* Two-level menu: genres at level 0, stations of the selected genre at
 * level 1. Stream URLs follow the official SomaFM playlist slugs
 * (api.somafm.com/<slug>.pls -> http://ice1.somafm.com/<slug>-128-mp3). */
typedef struct {
    const char* name;
    const char* url;
} RadioStationDef;

typedef struct {
    const char* name;
    const RadioStationDef* stations;
    uint8_t count;
} RadioGenreDef;

#define STATION_DEF(name, slug) \
    {name, "http://ice1.somafm.com/" slug "-128-mp3"}

static const RadioStationDef genre_ambient[] = {
    STATION_DEF("Drone Zone", "dronezone"),
    STATION_DEF("Deep Space One", "deepspaceone"),
    STATION_DEF("DEF CON Radio", "defcon"),
    STATION_DEF("Mission Control", "missioncontrol"),
};

static const RadioStationDef genre_downtempo[] = {
    STATION_DEF("Groove Salad", "groovesalad"),
    STATION_DEF("Vaporwaves", "vaporwaves"),
    STATION_DEF("Fluid", "fluid"),
    STATION_DEF("Beat Blender", "beatblender"),
};

static const RadioStationDef genre_lounge[] = {
    STATION_DEF("Illinois Street Lounge", "illstreet"),
    STATION_DEF("Left Coast 70s", "seventies"),
    STATION_DEF("Secret Agent", "secretagent"),
    STATION_DEF("Lush", "lush"),
};

static const RadioStationDef genre_electronic[] = {
    STATION_DEF("The Trip", "thetrip"),
    STATION_DEF("cliqhop IDM", "cliqhop"),
    STATION_DEF("Dub Step Beyond", "dubstep"),
    STATION_DEF("Dark Zone", "darkzone"),
};

static const RadioStationDef genre_poprock[] = {
    STATION_DEF("Boot Liquor", "bootliquor"),
    STATION_DEF("PopTron", "poptron"),
    STATION_DEF("Underground 80s", "u80s"),
    STATION_DEF("Indie Pop", "indiepop"),
};

static const RadioStationDef genre_metal[] = {
    STATION_DEF("Metal Detector", "metal"),
    STATION_DEF("Doomed", "doomed"),
};

#define GENRE_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static const RadioGenreDef radio_genres[] = {
    {"Ambient", genre_ambient, GENRE_COUNT(genre_ambient)},
    {"Downtempo", genre_downtempo, GENRE_COUNT(genre_downtempo)},
    {"Lounge", genre_lounge, GENRE_COUNT(genre_lounge)},
    {"Electronic", genre_electronic, GENRE_COUNT(genre_electronic)},
    {"Pop / Rock", genre_poprock, GENRE_COUNT(genre_poprock)},
    {"Metal", genre_metal, GENRE_COUNT(genre_metal)},
};

#define RADIO_GENRE_COUNT (sizeof(radio_genres) / sizeof(radio_genres[0]))

static void radio_menu_callback(void* context, uint32_t index) {
    RadioApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, index);
}

static void radio_switch(RadioApp* app, RadioView view) {
    app->current_view = view;
    view_dispatcher_switch_to_view(app->dispatcher, view);
}

static void radio_rebuild_menu(RadioApp* app, const char* status) {
    submenu_reset(app->submenu);
    if(status && status[0]) {
        strncpy(app->header, status, sizeof(app->header) - 1);
        app->header[sizeof(app->header) - 1] = '\0';
    } else if(app->menu_level == 0) {
        snprintf(app->header, sizeof(app->header), "%u genres", (unsigned)RADIO_GENRE_COUNT);
    } else {
        const RadioGenreDef* g = &radio_genres[app->cur_genre];
        snprintf(
            app->header, sizeof(app->header), "%s (%u)", g->name, (unsigned)g->count);
    }
    submenu_set_header(app->submenu, app->header);
    if(app->menu_level == 0) {
        for(uint8_t i = 0; i < RADIO_GENRE_COUNT; i++) {
            submenu_add_item(
                app->submenu, radio_genres[i].name, RadioEventGenreBase + i,
                radio_menu_callback, app);
        }
        submenu_add_item(
            app->submenu, "Custom URL...", RadioEventCustomURL, radio_menu_callback, app);
    } else {
        const RadioGenreDef* g = &radio_genres[app->cur_genre];
        for(uint8_t i = 0; i < g->count; i++) {
            submenu_add_item(
                app->submenu, g->stations[i].name, RadioEventStationBase + i,
                radio_menu_callback, app);
        }
        submenu_add_item(
            app->submenu, "< All genres", RadioEventBackToGenres, radio_menu_callback, app);
    }
}

static int32_t radio_play_worker(void* ctx) {
    RadioApp* app = ctx;
    char url[RADIO_URL_LEN];
    strncpy(url, app->text_buf, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    radio_stream_play(app, url);
    return 0;
}

static void radio_stop_worker(RadioApp* app) {
    if(app->worker) {
        app->worker_run = false;
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
    }
    radio_view_set_status(app->player_view, 0, app->volume, "Stopped");
}

static void radio_stop_callback(void* context) {
    RadioApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, RadioEventStop);
}

static void radio_vol_callback(void* context, int8_t delta) {
    RadioApp* app = context;
    int v = (int)app->volume + delta;
    if(v < 0) v = 0;
    if(v > 150) v = 150;
    app->volume = (uint8_t)v;
    mp3_sink_set_volume(app->volume);
}

static void radio_text_done_callback(void* context) {
    RadioApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, RadioEventCustomURL + 1);
}

static bool radio_custom_event_callback(void* context, uint32_t event) {
    RadioApp* app = context;
    if(event == RadioEventStop) {
        radio_stop_worker(app);
        radio_rebuild_menu(app, NULL);
        radio_switch(app, RadioViewSubmenu);
        return true;
    }
    if(event == RadioEventBackToGenres ||
       (event >= RadioEventGenreBase && event < RadioEventGenreBase + 32)) {
        if(event == RadioEventBackToGenres) {
            app->menu_level = 0;
        } else {
            uint8_t gi = (uint8_t)(event - RadioEventGenreBase);
            if(gi < RADIO_GENRE_COUNT) {
                app->cur_genre = gi;
                app->menu_level = 1;
            }
        }
        radio_rebuild_menu(app, NULL);
        return true;
    }
    if(event == RadioEventCustomURL) {
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Stream URL");
        text_input_set_result_callback(
            app->text_input, radio_text_done_callback, app, app->text_buf,
            sizeof(app->text_buf), true);
        radio_switch(app, RadioViewInput);
        return true;
    }
    if(event == RadioEventCustomURL + 1) {
        /* Custom URL entered: play it directly. */
        if(app->text_buf[0] == '\0') {
            radio_switch(app, RadioViewSubmenu);
            return true;
        }
        Wifi* wifi = furi_record_open(RECORD_WIFI);
        bool connected = wifi_is_connected(wifi);
        furi_record_close(RECORD_WIFI);
        if(!connected) {
            radio_rebuild_menu(app, "WiFi offline");
            radio_switch(app, RadioViewSubmenu);
            return true;
        }
        radio_view_set_station(app->player_view, "Custom");
        radio_view_set_track(app->player_view, app->text_buf);
        radio_view_set_status(app->player_view, 0, app->volume, "Buffering...");
        radio_switch(app, RadioViewPlayer);
        app->worker_run = true;
        /* Stash URL in text_buf (already there); worker reads it. */
        app->worker = furi_thread_alloc_ex("RadioPlay", 4096, radio_play_worker, app);
        furi_thread_start(app->worker);
        return true;
    }
    if(event >= RadioEventStationBase && event < RadioEventStationBase + RADIO_MAX_STATIONS) {
        const RadioGenreDef* g = &radio_genres[app->cur_genre];
        uint8_t idx = (uint8_t)(event - RadioEventStationBase);
        if(app->menu_level != 1 || idx >= g->count) return true;
        Wifi* wifi = furi_record_open(RECORD_WIFI);
        bool connected = wifi_is_connected(wifi);
        furi_record_close(RECORD_WIFI);
        if(!connected) {
            radio_rebuild_menu(app, "WiFi offline");
            radio_switch(app, RadioViewSubmenu);
            return true;
        }
        strncpy(app->text_buf, g->stations[idx].url, sizeof(app->text_buf) - 1);
        app->text_buf[sizeof(app->text_buf) - 1] = '\0';
        radio_view_set_station(app->player_view, g->stations[idx].name);
        radio_view_set_track(app->player_view, "");
        radio_view_set_status(app->player_view, 0, app->volume, "Buffering...");
        radio_switch(app, RadioViewPlayer);
        app->worker_run = true;
        app->worker = furi_thread_alloc_ex("RadioPlay", 4096, radio_play_worker, app);
        furi_thread_start(app->worker);
        return true;
    }
    return false;
}

static bool radio_back_event_callback(void* context) {
    RadioApp* app = context;
    if(app->current_view == RadioViewSubmenu && app->menu_level == 1) {
        /* Back from a station list returns to the genre list. */
        app->menu_level = 0;
        radio_rebuild_menu(app, NULL);
        return true;
    }
    radio_stop_worker(app);
    return false;
}

static RadioApp* radio_alloc(void) {
    RadioApp* app = malloc(sizeof(RadioApp));
    memset(app, 0, sizeof(RadioApp));
    app->volume = 100;
    app->current_view = RadioViewSubmenu;

    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    app->submenu = submenu_alloc();
    app->text_input = text_input_alloc();
    app->player_view = radio_view_alloc();

    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, radio_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, radio_back_event_callback);

    radio_view_set_stop_callback(app->player_view, radio_stop_callback, app);
    radio_view_set_vol_callback(app->player_view, radio_vol_callback, app);

    view_dispatcher_add_view(
        app->dispatcher, RadioViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_add_view(
        app->dispatcher, RadioViewInput, text_input_get_view(app->text_input));
    view_dispatcher_add_view(
        app->dispatcher, RadioViewPlayer, radio_view_get_view(app->player_view));

    radio_rebuild_menu(app, NULL);

    /* Select the initial view (same viewless-dispatcher trap as voice memo). */
    radio_switch(app, RadioViewSubmenu);

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    return app;
}

static void radio_free(RadioApp* app) {
    radio_stop_worker(app);
    view_dispatcher_remove_view(app->dispatcher, RadioViewSubmenu);
    view_dispatcher_remove_view(app->dispatcher, RadioViewInput);
    view_dispatcher_remove_view(app->dispatcher, RadioViewPlayer);
    radio_view_free(app->player_view);
    text_input_free(app->text_input);
    submenu_free(app->submenu);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t radio_app(void* p) {
    UNUSED(p);
    FURI_LOG_I("Radio", "starting");
    RadioApp* app = radio_alloc();
    view_dispatcher_run(app->dispatcher);
    radio_free(app);
    FURI_LOG_I("Radio", "exit");
    return 0;
}

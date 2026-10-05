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

static const RadioStation radio_default_stations[] = {
    {"Groove Salad", "http://ice1.somafm.com/groovesalad-128-mp3"},
    {"Drone Zone", "http://ice1.somafm.com/dronezone-128-mp3"},
    {"Secret Agent", "http://ice1.somafm.com/secretagent-128-mp3"},
    {"Deep Space One", "http://ice1.somafm.com/deepspaceone-128-mp3"},
    {"Boot Liquor", "http://ice1.somafm.com/bootliquor-128-mp3"},
    {"Vaporwaves", "http://ice1.somafm.com/vaporwaves-128-mp3"},
    {"Metal Detector", "http://ice1.somafm.com/metal-128-mp3"},
};

static void radio_menu_callback(void* context, uint32_t index) {
    RadioApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, index);
}

static void radio_rebuild_menu(RadioApp* app, const char* status) {
    submenu_reset(app->submenu);
    if(status && status[0]) {
        strncpy(app->header, status, sizeof(app->header) - 1);
        app->header[sizeof(app->header) - 1] = '\0';
    } else if(app->station_count == 0) {
        snprintf(app->header, sizeof(app->header), "No stations");
    } else {
        snprintf(
            app->header, sizeof(app->header), "%u station%s", (unsigned)app->station_count,
            app->station_count == 1 ? "" : "s");
    }
    submenu_set_header(app->submenu, app->header);
    for(uint8_t i = 0; i < app->station_count; i++) {
        submenu_add_item(
            app->submenu, app->stations[i].name, RadioEventStationBase + i,
            radio_menu_callback, app);
    }
    submenu_add_item(
        app->submenu, "Custom URL...", RadioEventCustomURL, radio_menu_callback, app);
}

static void radio_install_menu_callbacks(RadioApp* app) {
    radio_rebuild_menu(app, NULL);
    /* Callbacks were attached by radio_rebuild_menu; nothing more to do. */
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
    radio_view_set_status(app->player_view, 0, 80, "Stopped");
}

static void radio_stop_callback(void* context) {
    RadioApp* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, RadioEventStop);
}

static void radio_vol_callback(void* context, int8_t delta) {
    RadioApp* app = context;
    int v = (int)app->volume + delta;
    if(v < 0) v = 0;
    if(v > 100) v = 100;
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
        radio_install_menu_callbacks(app);
        view_dispatcher_switch_to_view(app->dispatcher, RadioViewSubmenu);
        return true;
    }
    if(event == RadioEventCustomURL) {
        text_input_reset(app->text_input);
        text_input_set_header_text(app->text_input, "Stream URL");
        text_input_set_result_callback(
            app->text_input, radio_text_done_callback, app, app->text_buf,
            sizeof(app->text_buf), true);
        view_dispatcher_switch_to_view(app->dispatcher, RadioViewInput);
        return true;
    }
    if(event == RadioEventCustomURL + 1) {
        /* Custom URL entered: play it directly. */
        if(app->text_buf[0] == '\0') {
            view_dispatcher_switch_to_view(app->dispatcher, RadioViewSubmenu);
            return true;
        }
        Wifi* wifi = furi_record_open(RECORD_WIFI);
        bool connected = wifi_is_connected(wifi);
        furi_record_close(RECORD_WIFI);
        if(!connected) {
            radio_rebuild_menu(app, "WiFi offline");
            radio_install_menu_callbacks(app);
            view_dispatcher_switch_to_view(app->dispatcher, RadioViewSubmenu);
            return true;
        }
        radio_view_set_station(app->player_view, "Custom");
        radio_view_set_track(app->player_view, app->text_buf);
        radio_view_set_status(app->player_view, 0, 80, "Buffering...");
        view_dispatcher_switch_to_view(app->dispatcher, RadioViewPlayer);
        app->worker_run = true;
        /* Stash URL in text_buf (already there); worker reads it. */
        app->worker = furi_thread_alloc_ex("RadioPlay", 4096, radio_play_worker, app);
        furi_thread_start(app->worker);
        return true;
    }
    if(event >= RadioEventStationBase && event < RadioEventStationBase + RADIO_MAX_STATIONS) {
        uint8_t idx = (uint8_t)(event - RadioEventStationBase);
        if(idx >= app->station_count) return true;
        Wifi* wifi = furi_record_open(RECORD_WIFI);
        bool connected = wifi_is_connected(wifi);
        furi_record_close(RECORD_WIFI);
        if(!connected) {
            radio_rebuild_menu(app, "WiFi offline");
            radio_install_menu_callbacks(app);
            view_dispatcher_switch_to_view(app->dispatcher, RadioViewSubmenu);
            return true;
        }
        strncpy(app->text_buf, app->stations[idx].url, sizeof(app->text_buf) - 1);
        radio_view_set_station(app->player_view, app->stations[idx].name);
        radio_view_set_track(app->player_view, "");
        radio_view_set_status(app->player_view, 0, 80, "Buffering...");
        view_dispatcher_switch_to_view(app->dispatcher, RadioViewPlayer);
        app->worker_run = true;
        app->worker = furi_thread_alloc_ex("RadioPlay", 4096, radio_play_worker, app);
        furi_thread_start(app->worker);
        return true;
    }
    return false;
}

static bool radio_back_event_callback(void* context) {
    RadioApp* app = context;
    radio_stop_worker(app);
    return false;
}

static RadioApp* radio_alloc(void) {
    RadioApp* app = malloc(sizeof(RadioApp));
    memset(app, 0, sizeof(RadioApp));
    app->volume = 80;

    size_t n = sizeof(radio_default_stations) / sizeof(radio_default_stations[0]);
    if(n > RADIO_MAX_STATIONS) n = RADIO_MAX_STATIONS;
    for(size_t i = 0; i < n; i++) {
        app->stations[i] = radio_default_stations[i];
    }
    app->station_count = (uint8_t)n;

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
    radio_install_menu_callbacks(app);

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

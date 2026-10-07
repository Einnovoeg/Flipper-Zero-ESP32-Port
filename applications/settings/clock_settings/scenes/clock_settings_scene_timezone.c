#include "../clock_settings.h"
#include <furi_hal.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "ClockTzScene"

/* Whole display offsets in 15-minute steps: UTC-12:00 .. UTC+14:00. */
#define TZ_MIN_MINUTES (-12 * 60)
#define TZ_MAX_MINUTES (14 * 60)
#define TZ_STEP_MINUTES 15
#define TZ_CUSTOM_BASE 1000u

static void clock_settings_timezone_format(int32_t minutes, char* out, size_t outsz) {
    if(minutes == 0) {
        snprintf(out, outsz, "UTC (no offset)");
        return;
    }
    char sign = minutes < 0 ? '-' : '+';
    int absmin = minutes < 0 ? -minutes : minutes;
    snprintf(out, outsz, "UTC%c%02d:%02d", sign, absmin / 60, absmin % 60);
}

static void clock_settings_timezone_callback(void* context, uint32_t index) {
    ClockSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TZ_CUSTOM_BASE + index);
}

void clock_settings_scene_timezone_on_enter(void* context) {
    ClockSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Display timezone");

    int32_t current = furi_hal_rtc_get_timezone_offset();
    uint32_t select = 0;
    uint32_t idx = 0;
    char label[24];
    for(int32_t m = TZ_MIN_MINUTES; m <= TZ_MAX_MINUTES; m += TZ_STEP_MINUTES) {
        clock_settings_timezone_format(m, label, sizeof(label));
        submenu_add_item(app->submenu, label, idx, clock_settings_timezone_callback, app);
        if(m == current) select = idx;
        idx++;
    }
    submenu_set_selected_item(app->submenu, select);
    view_dispatcher_switch_to_view(app->view_dispatcher, ClockSettingsViewSubmenu);
}

bool clock_settings_scene_timezone_on_event(void* context, SceneManagerEvent event) {
    ClockSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event < TZ_CUSTOM_BASE) return false;
    uint32_t sel = event.event - TZ_CUSTOM_BASE;
    int32_t minutes = TZ_MIN_MINUTES + (int32_t)sel * TZ_STEP_MINUTES;
    if(minutes < TZ_MIN_MINUTES || minutes > TZ_MAX_MINUTES) return false;

    furi_hal_rtc_set_timezone_offset(minutes);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool saved = false;
    if(storage_file_open(file, "/int/.timezone", FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%ld\n", (long)minutes);
        saved = storage_file_write(file, buf, (size_t)len) == (size_t)len;
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    FURI_LOG_I(
        TAG,
        "timezone UTC%+d:%02u %s",
        (int)(minutes / 60),
        (unsigned)(abs(minutes) % 60),
        saved ? "saved" : "APPLY-ONLY (save failed)");
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void clock_settings_scene_timezone_on_exit(void* context) {
    UNUSED(context);
}

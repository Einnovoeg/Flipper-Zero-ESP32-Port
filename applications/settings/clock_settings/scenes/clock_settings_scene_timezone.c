#include "../clock_settings.h"
#include <furi_hal.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "ClockTzScene"

#define TZ_CUSTOM_BASE 0x100u
#define TZ_PATH "/int/.timezone"

typedef struct {
    const char* name;
    int16_t base; /* standard-time minutes east of UTC */
    uint8_t dst; /* FURI_HAL_RTC_TZ_DST_* */
} TzCity;

/* Curated world cities — one entry each (no 15-minute scrolling). Offsets are
 * standard time; DST zones auto-adjust the displayed clock via the RTC rule. */
static const TzCity tz_cities[] = {
    /* North America */
    {"Los Angeles", -480, FURI_HAL_RTC_TZ_DST_US},
    {"Vancouver", -480, FURI_HAL_RTC_TZ_DST_US},
    {"Denver", -420, FURI_HAL_RTC_TZ_DST_US},
    {"Phoenix", -420, FURI_HAL_RTC_TZ_DST_NONE},
    {"Chicago", -360, FURI_HAL_RTC_TZ_DST_US},
    {"Mexico City", -360, FURI_HAL_RTC_TZ_DST_NONE},
    {"New York", -300, FURI_HAL_RTC_TZ_DST_US},
    {"Toronto", -300, FURI_HAL_RTC_TZ_DST_US},
    {"Miami", -300, FURI_HAL_RTC_TZ_DST_US},
    {"Anchorage", -540, FURI_HAL_RTC_TZ_DST_US},
    {"Honolulu", -600, FURI_HAL_RTC_TZ_DST_NONE},
    /* Europe */
    {"Reykjavik", 0, FURI_HAL_RTC_TZ_DST_NONE},
    {"London", 0, FURI_HAL_RTC_TZ_DST_EU},
    {"Dublin", 0, FURI_HAL_RTC_TZ_DST_EU},
    {"Lisbon", 0, FURI_HAL_RTC_TZ_DST_EU},
    {"Paris", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Berlin", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Madrid", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Amsterdam", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Brussels", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Rome", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Zurich", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Vienna", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Prague", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Warsaw", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Stockholm", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Oslo", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Copenhagen", 60, FURI_HAL_RTC_TZ_DST_EU},
    {"Helsinki", 120, FURI_HAL_RTC_TZ_DST_EU},
    {"Athens", 120, FURI_HAL_RTC_TZ_DST_EU},
    {"Bucharest", 120, FURI_HAL_RTC_TZ_DST_EU},
    {"Istanbul", 180, FURI_HAL_RTC_TZ_DST_NONE},
    {"Moscow", 180, FURI_HAL_RTC_TZ_DST_NONE},
    /* Middle East / Asia */
    {"Dubai", 240, FURI_HAL_RTC_TZ_DST_NONE},
    {"Karachi", 300, FURI_HAL_RTC_TZ_DST_NONE},
    {"Delhi", 330, FURI_HAL_RTC_TZ_DST_NONE},
    {"Bangkok", 420, FURI_HAL_RTC_TZ_DST_NONE},
    {"Jakarta", 420, FURI_HAL_RTC_TZ_DST_NONE},
    {"Beijing", 480, FURI_HAL_RTC_TZ_DST_NONE},
    {"Hong Kong", 480, FURI_HAL_RTC_TZ_DST_NONE},
    {"Singapore", 480, FURI_HAL_RTC_TZ_DST_NONE},
    {"Perth", 480, FURI_HAL_RTC_TZ_DST_NONE},
    {"Tokyo", 540, FURI_HAL_RTC_TZ_DST_NONE},
    {"Seoul", 540, FURI_HAL_RTC_TZ_DST_NONE},
    /* Oceania */
    {"Sydney", 600, FURI_HAL_RTC_TZ_DST_AU},
    {"Melbourne", 600, FURI_HAL_RTC_TZ_DST_AU},
    {"Brisbane", 600, FURI_HAL_RTC_TZ_DST_NONE},
    {"Auckland", 720, FURI_HAL_RTC_TZ_DST_NONE},
};

#define TZ_CITY_COUNT (sizeof(tz_cities) / sizeof(tz_cities[0]))

static void tz_format_offset(int32_t minutes, char* out, size_t outsz) {
    if(minutes == 0) {
        snprintf(out, outsz, "UTC");
        return;
    }
    char sign = minutes < 0 ? '-' : '+';
    int absmin = (int)(minutes < 0 ? -minutes : minutes);
    snprintf(out, outsz, "UTC%c%02d:%02d", sign, absmin / 60, absmin % 60);
}

/* Read the stored city index (3rd field of /int/.timezone), else -1. */
static int tz_read_city_index(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    int index = -1;
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, TZ_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[32] = {0};
        size_t n = storage_file_read(file, buf, sizeof(buf) - 1);
        storage_file_close(file);
        if(n > 0) {
            int base = 0, dst = 0, idx = -1;
            if(sscanf(buf, "%d %d %d", &base, &dst, &idx) >= 3) index = idx;
        }
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return index;
}

static int tz_current_city_index(void) {
    int stored = tz_read_city_index();
    if(stored >= 0 && stored < (int)TZ_CITY_COUNT) return stored;
    /* Fall back to matching the active standard offset + rule. */
    int32_t base;
    uint8_t dst;
    furi_hal_rtc_get_timezone_zone(&base, &dst);
    for(size_t i = 0; i < TZ_CITY_COUNT; i++) {
        if(tz_cities[i].base == base && tz_cities[i].dst == dst) return (int)i;
    }
    return -1;
}

static void tz_save_city(size_t index) {
    const TzCity* c = &tz_cities[index];
    furi_hal_rtc_set_timezone_zone(c->base, c->dst);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool saved = false;
    if(storage_file_open(file, TZ_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[40];
        int len = snprintf(buf, sizeof(buf), "%d %d %u\n", c->base, c->dst, (unsigned)index);
        saved = storage_file_write(file, buf, (size_t)len) == (size_t)len;
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    int32_t eff = furi_hal_rtc_tz_effective_offset(c->base, c->dst);
    FURI_LOG_I(
        TAG,
        "timezone %s base=%d dst=%u eff=%d %s",
        c->name,
        (int)c->base,
        (unsigned)c->dst,
        (int)eff,
        saved ? "saved" : "APPLY-ONLY (save failed)");
}

static void clock_settings_timezone_callback(void* context, uint32_t index) {
    ClockSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TZ_CUSTOM_BASE + index);
}

void clock_settings_scene_timezone_on_enter(void* context) {
    ClockSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Select city");

    int current = tz_current_city_index();
    char label[48];
    for(size_t i = 0; i < TZ_CITY_COUNT; i++) {
        int32_t eff = furi_hal_rtc_tz_effective_offset(tz_cities[i].base, tz_cities[i].dst);
        char off[16];
        tz_format_offset(eff, off, sizeof(off));
        snprintf(label, sizeof(label), "%s (%s)", tz_cities[i].name, off);
        submenu_add_item(app->submenu, label, (uint32_t)i, clock_settings_timezone_callback, app);
    }
    if(current >= 0) submenu_set_selected_item(app->submenu, (uint32_t)current);
    view_dispatcher_switch_to_view(app->view_dispatcher, ClockSettingsViewSubmenu);
}

bool clock_settings_scene_timezone_on_event(void* context, SceneManagerEvent event) {
    ClockSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event < TZ_CUSTOM_BASE) return false;
    uint32_t sel = event.event - TZ_CUSTOM_BASE;
    if(sel >= TZ_CITY_COUNT) return false;

    tz_save_city(sel);
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void clock_settings_scene_timezone_on_exit(void* context) {
    UNUSED(context);
}

#include "../clock_settings.h"
#include <furi_hal.h>

typedef enum {
    ClockSettingsStartBacklight,
    ClockSettingsStartTimezone,
} ClockSettingsStartIndex;

static void clock_settings_start_callback(void* context, uint32_t index) {
    ClockSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void clock_settings_scene_start_on_enter(void* context) {
    ClockSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Clock settings");
    submenu_add_item(
        app->submenu, "Backlight", ClockSettingsStartBacklight, clock_settings_start_callback,
        app);
    submenu_add_item(
        app->submenu, "Timezone", ClockSettingsStartTimezone, clock_settings_start_callback,
        app);
    submenu_set_selected_item(app->submenu, 0);
    view_dispatcher_switch_to_view(app->view_dispatcher, ClockSettingsViewSubmenu);
}

bool clock_settings_scene_start_on_event(void* context, SceneManagerEvent event) {
    ClockSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == ClockSettingsStartBacklight) {
        view_dispatcher_switch_to_view(app->view_dispatcher, ClockSettingsViewPwm);
        return true;
    }
    if(event.event == ClockSettingsStartTimezone) {
        scene_manager_next_scene(app->scene_manager, ClockSettingsSceneTimezone);
        return true;
    }
    return false;
}

void clock_settings_scene_start_on_exit(void* context) {
    UNUSED(context);
}

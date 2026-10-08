#include "../clock_settings.h"

#define TONE_CUSTOM_BASE 0x200u

static void clock_settings_tone_callback(void* context, uint32_t index) {
    ClockSettings* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, TONE_CUSTOM_BASE + index);
}

void clock_settings_scene_tone_on_enter(void* context) {
    ClockSettings* app = context;
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Alarm tone");

    for(uint8_t i = 0; i < CLOCK_SETTINGS_TONE_COUNT; i++) {
        submenu_add_item(
            app->submenu,
            clock_settings_tone_name(i),
            (uint32_t)i,
            clock_settings_tone_callback,
            app);
    }
    submenu_set_selected_item(app->submenu, clock_settings_get_tone());
    view_dispatcher_switch_to_view(app->view_dispatcher, ClockSettingsViewSubmenu);
}

bool clock_settings_scene_tone_on_event(void* context, SceneManagerEvent event) {
    ClockSettings* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event < TONE_CUSTOM_BASE) return false;
    uint32_t sel = event.event - TONE_CUSTOM_BASE;
    if(sel >= CLOCK_SETTINGS_TONE_COUNT) return true;

    clock_settings_set_tone((uint8_t)sel);
    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void clock_settings_scene_tone_on_exit(void* context) {
    UNUSED(context);
}

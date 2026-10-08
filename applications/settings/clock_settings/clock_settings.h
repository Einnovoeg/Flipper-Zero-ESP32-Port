#pragma once

#include "scenes/clock_settings_scene.h"

#include <furi_hal_rtc.h>
#include <furi_hal_pwm.h>

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/submenu.h>
#include "views/clock_settings_module.h"

typedef struct ClockSettings ClockSettings;

struct ClockSettings {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    ClockSettingsModule* pwm_view;
    Submenu* submenu;
};

typedef enum {
    ClockSettingsViewPwm,
    ClockSettingsViewSubmenu,
} ClockSettingsView;

typedef enum {
    ClockSettingsCustomEventNone,
    ClockSettingsCustomEventPwm,
    ClockSettingsCustomEventTimezone,
    ClockSettingsCustomEventTimezoneSaved,
} ClockSettingsCustomEvent;

/* Alarm tone presets — implemented in clock_settings_alarm.c (the alarm
 * service also owns persistence of the armed time + tone under /int). */
#define CLOCK_SETTINGS_TONE_COUNT 4
uint8_t clock_settings_get_tone(void);
void clock_settings_set_tone(uint8_t tone);
const char* clock_settings_tone_name(uint8_t tone);

#pragma once

#include <stdint.h>

#define DISPLAY_BATTERY_OFF                6
#define DISPLAY_BATTERY_BAR                0
#define DISPLAY_BATTERY_PERCENT            1
#define DISPLAY_BATTERY_INVERTED_PERCENT   2
#define DISPLAY_BATTERY_RETRO_3            3
#define DISPLAY_BATTERY_RETRO_5            4
#define DISPLAY_BATTERY_BAR_PERCENT        5

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FavoriteAppLeftShort,
    FavoriteAppLeftLong,
    FavoriteAppRightShort,
    FavoriteAppRightLong,
    FavoriteAppOkLong,

    FavoriteAppNumber,
} FavoriteAppShortcut;

typedef enum {
    DummyAppLeftShort,
    DummyAppLeftLong,
    DummyAppRightShort,
    DummyAppRightLong,
    DummyAppUpLong,
    DummyAppDownShort,
    DummyAppDownLong,
    DummyAppOkShort,
    DummyAppOkLong,

    DummyAppNumber,
} DummyAppShortcut;

typedef struct {
    char name_or_path[128];
} FavoriteApp;

/** Color themes: named presets the Interface settings picker offers. The
 * picker maps them onto the notification UI colors (the source of truth
 * for the LCD fg/bg) — see color_theme_changed in interface_settings_app.c. */
typedef enum {
    DesktopThemeOrange = 0,
    DesktopThemeGreen,
    DesktopThemeAmber,
    DesktopThemeCyan,
    DesktopThemeRed,
    DesktopThemeWhite,
    DesktopThemeInvert,
    DesktopThemeCount,
} DesktopTheme;

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t usb_inhibit_auto_lock;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    uint8_t displayTheme;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[DummyAppNumber];
} DesktopSettings;

void desktop_settings_load(DesktopSettings* settings);
void desktop_settings_save(const DesktopSettings* settings);

/** Human-readable name for a theme index (clamped to Orange). */
const char* desktop_color_theme_name(uint8_t theme);

/** Re-apply the notification UI colors (the single source of truth for
 * fg/bg). Called at boot and whenever settings are saved, so a user-chosen
 * custom color is never clobbered by the desktop theme. */
void desktop_settings_apply_theme(const DesktopSettings* settings);

#ifdef __cplusplus
}
#endif

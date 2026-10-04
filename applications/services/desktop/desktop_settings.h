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

/** Color themes (index into desktop_color_themes in desktop_settings.c).
 * Theme 0 is the classic Flipper orange. All values are board-native RGB565
 * (same byte order convention as BOARD_LCD_FG/BG_COLOR). */
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

/** Apply settings->displayTheme to the LCD (clamped). Called at boot and
 * whenever the Interface settings change it. */
void desktop_settings_apply_theme(const DesktopSettings* settings);

#ifdef __cplusplus
}
#endif

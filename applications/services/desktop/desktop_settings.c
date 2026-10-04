#include "desktop_settings.h"
#include "desktop_settings_filename.h"

#include <saved_struct.h>
#include <storage/storage.h>
#include <furi_hal_display.h>

#define TAG "DesktopSettings"

#define DESKTOP_SETTINGS_VER_14 (14)
#define DESKTOP_SETTINGS_VER_17 (17)
#define DESKTOP_SETTINGS_VER    (18)

#define DESKTOP_SETTINGS_PATH  INT_PATH(DESKTOP_SETTINGS_FILE_NAME)
#define DESKTOP_SETTINGS_MAGIC (0x17)

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[DummyAppNumber];
} DesktopSettingsV14;

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t usb_inhibit_auto_lock;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[DummyAppNumber];
} DesktopSettingsV17;

// Actual size of DesktopSettings v13
//static_assert(sizeof(DesktopSettingsV13) == 1234);

void desktop_settings_load(DesktopSettings* settings) {
    furi_assert(settings);

    bool success = false;

    do {
        uint8_t version;
        if(!saved_struct_get_metadata(DESKTOP_SETTINGS_PATH, NULL, &version, NULL)) break;

        if(version == DESKTOP_SETTINGS_VER) {
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                sizeof(DesktopSettings),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER);

        } else if(version == DESKTOP_SETTINGS_VER_17) {
            DesktopSettingsV17* settings_v17 = malloc(sizeof(DesktopSettingsV17));

            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings_v17,
                sizeof(DesktopSettingsV17),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_17);

            if(success) {
                settings->auto_lock_delay_ms = settings_v17->auto_lock_delay_ms;
                settings->usb_inhibit_auto_lock = settings_v17->usb_inhibit_auto_lock;
                settings->displayBatteryPercentage = settings_v17->displayBatteryPercentage;
                settings->dummy_mode = settings_v17->dummy_mode;
                settings->display_clock = settings_v17->display_clock;
                settings->displayTheme = DesktopThemeOrange;
                memcpy(
                    settings->favorite_apps,
                    settings_v17->favorite_apps,
                    sizeof(settings->favorite_apps));
                memcpy(
                    settings->dummy_apps, settings_v17->dummy_apps, sizeof(settings->dummy_apps));
            }

            free(settings_v17);
        } else if(version == DESKTOP_SETTINGS_VER_14) {
            DesktopSettingsV14* settings_v14 = malloc(sizeof(DesktopSettingsV14));

            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings_v14,
                sizeof(DesktopSettingsV14),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_14);

            if(success) {
                settings->auto_lock_delay_ms = settings_v14->auto_lock_delay_ms;
                settings->usb_inhibit_auto_lock = 0;
                settings->displayBatteryPercentage = settings_v14->displayBatteryPercentage;
                settings->dummy_mode = settings_v14->dummy_mode;
                settings->display_clock = settings_v14->display_clock;
                settings->displayTheme = DesktopThemeOrange;
                memcpy(
                    settings->favorite_apps,
                    settings_v14->favorite_apps,
                    sizeof(settings->favorite_apps));
                memcpy(
                    settings->dummy_apps, settings_v14->dummy_apps, sizeof(settings->dummy_apps));
            }

            free(settings_v14);
        }

    } while(false);

    if(!success) {
        FURI_LOG_W(TAG, "Failed to load file, using defaults");
        memset(settings, 0, sizeof(DesktopSettings));
        desktop_settings_save(settings);
    }
}

void desktop_settings_save(const DesktopSettings* settings) {
    furi_assert(settings);

    const bool success = saved_struct_save(
        DESKTOP_SETTINGS_PATH,
        settings,
        sizeof(DesktopSettings),
        DESKTOP_SETTINGS_MAGIC,
        DESKTOP_SETTINGS_VER);

    if(!success) {
        FURI_LOG_E(TAG, "Failed to save file");
    }
}

/* Color themes: (fg, bg) in board-native RGB565 order (same convention as
 * BOARD_LCD_FG/BG_COLOR). fg fills unset pixels (screen background),
 * bg fills set pixels (UI elements). */
typedef struct {
    uint16_t fg;
    uint16_t bg;
} DesktopColorTheme;

static const DesktopColorTheme desktop_color_themes[DesktopThemeCount] = {
    [DesktopThemeOrange] = {0xA0FD, 0x0000}, /* Flipper orange / black */
    [DesktopThemeGreen] = {0xE007, 0x0000}, /* green / black */
    [DesktopThemeAmber] = {0x60FD, 0x0000}, /* amber / black */
    [DesktopThemeCyan] = {0xFF07, 0x0000}, /* cyan / black */
    [DesktopThemeRed] = {0x00F8, 0x0000}, /* red / black */
    [DesktopThemeWhite] = {0xFFFF, 0x0000}, /* white / black */
    [DesktopThemeInvert] = {0x0000, 0xFFFF}, /* black / white (dark mode) */
};

const char* desktop_color_theme_name(uint8_t theme) {
    switch(theme) {
    case DesktopThemeOrange:
        return "Orange";
    case DesktopThemeGreen:
        return "Green";
    case DesktopThemeAmber:
        return "Amber";
    case DesktopThemeCyan:
        return "Cyan";
    case DesktopThemeRed:
        return "Red";
    case DesktopThemeWhite:
        return "White";
    case DesktopThemeInvert:
        return "Invert";
    default:
        return "Orange";
    }
}

void desktop_settings_apply_theme(const DesktopSettings* settings) {
    furi_assert(settings);
    uint8_t theme = settings->displayTheme;
    if(theme >= DesktopThemeCount) theme = DesktopThemeOrange;
    furi_hal_display_set_fg_color(desktop_color_themes[theme].fg);
    furi_hal_display_set_bg_color(desktop_color_themes[theme].bg);
}

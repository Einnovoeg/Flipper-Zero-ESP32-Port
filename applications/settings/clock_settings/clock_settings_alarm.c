#include "clock_settings.h"

#include <furi.h>
#include <furi_hal.h>

#include <gui/gui.h>
#include <gui/view_port.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <assets_icons.h>
#include <storage/storage.h>

#include <stdio.h>

#define TAG "ClockSettingsAlarm"

#define SNOOZE_MINUTES  9
#define TIMEOUT_MINUTES 10

#define ALARM_PATH "/int/alarm.settings"

typedef struct {
    DateTime now;
    DateTime snooze_until;
    DateTime alarm_start;
    IconAnimation* icon;

    bool is_snooze;
} ClockSettingsAlramModel;

/* --- Ring tones ------------------------------------------------------- */

static const NotificationSequence sequence_tone_classic = {
    &message_force_speaker_volume_setting_1f,
    &message_force_vibro_setting_on,
    &message_force_display_brightness_setting_1f,
    &message_vibro_on,

    &message_display_backlight_on,
    &message_note_c7,
    &message_delay_250,

    &message_display_backlight_off,
    &message_note_c4,
    &message_delay_250,

    &message_display_backlight_on,
    &message_note_c7,
    &message_delay_250,

    &message_display_backlight_off,
    &message_note_c4,
    &message_delay_250,

    &message_sound_off,
    &message_vibro_off,
    NULL,
};

static const NotificationSequence sequence_tone_beep = {
    &message_force_speaker_volume_setting_1f,
    &message_force_vibro_setting_on,
    &message_force_display_brightness_setting_1f,

    &message_display_backlight_on,
    &message_note_c8,
    &message_delay_100,
    &message_note_c8,
    &message_delay_100,
    &message_note_c8,
    &message_delay_100,

    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,

    &message_display_backlight_off,
    &message_sound_off,
    NULL,
};

static const NotificationSequence sequence_tone_chirp = {
    &message_force_speaker_volume_setting_1f,
    &message_force_vibro_setting_on,
    &message_force_display_brightness_setting_1f,

    &message_display_backlight_on,
    &message_note_c5,
    &message_delay_100,
    &message_note_e5,
    &message_delay_100,
    &message_note_g5,
    &message_delay_100,
    &message_note_c6,
    &message_delay_250,

    &message_display_backlight_off,
    &message_note_c5,
    &message_delay_100,
    &message_note_e5,
    &message_delay_100,
    &message_note_g5,
    &message_delay_100,
    &message_note_c6,
    &message_delay_250,

    &message_vibro_on,
    &message_delay_100,
    &message_vibro_off,

    &message_sound_off,
    NULL,
};

static const NotificationSequence sequence_tone_vibrate = {
    &message_force_speaker_volume_setting_1f,
    &message_force_vibro_setting_on,
    &message_force_display_brightness_setting_1f,

    &message_display_backlight_on,
    &message_vibro_on,
    &message_delay_250,
    &message_vibro_off,
    &message_delay_100,
    &message_vibro_on,
    &message_delay_250,
    &message_vibro_off,

    &message_display_backlight_off,
    &message_sound_off,
    NULL,
};

static const NotificationSequence* const alarm_tones[CLOCK_SETTINGS_TONE_COUNT] = {
    &sequence_tone_classic,
    &sequence_tone_beep,
    &sequence_tone_chirp,
    &sequence_tone_vibrate,
};

static const char* const alarm_tone_names[CLOCK_SETTINGS_TONE_COUNT] = {
    "Classic",
    "Beep",
    "Chirp",
    "Vibrate",
};

static const NotificationSequence* alarm_sequence(void) {
    uint8_t tone = clock_settings_get_tone();
    return alarm_tones[tone < CLOCK_SETTINGS_TONE_COUNT ? tone : 0];
}

/* --- Persistence ------------------------------------------------------ *
 * The RTC alarm is a RAM-only stub on this port, so the armed time and the
 * selected tone are mirrored to /int. The 1 s poller below loads them at
 * boot, re-saves on change, and fires the alarm on an exact minute match. */

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t enabled;
    uint8_t tone;
} ClockAlarmPersist;

static ClockAlarmPersist alarm_persist;
static bool alarm_loaded = false;
static bool alarm_dirty = false;
static uint32_t alarm_last_fired_key = 0;
static FuriTimer* alarm_poll_timer = NULL;

static void clock_settings_alarm_isr(void* context);

uint8_t clock_settings_get_tone(void) {
    return alarm_persist.tone < CLOCK_SETTINGS_TONE_COUNT ? alarm_persist.tone : 0;
}

void clock_settings_set_tone(uint8_t tone) {
    if(tone >= CLOCK_SETTINGS_TONE_COUNT) tone = 0;
    alarm_persist.tone = tone;
    alarm_dirty = true;
    FURI_LOG_I(TAG, "tone -> %s", alarm_tone_names[tone]);
}

const char* clock_settings_tone_name(uint8_t tone) {
    return alarm_tone_names[tone < CLOCK_SETTINGS_TONE_COUNT ? tone : 0];
}

static void clock_settings_alarm_load(void) {
    alarm_persist.hour = 0;
    alarm_persist.minute = 0;
    alarm_persist.enabled = 0;
    alarm_persist.tone = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, ALARM_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[48] = {0};
        size_t n = storage_file_read(file, buf, sizeof(buf) - 1);
        storage_file_close(file);
        int h = 0, m = 0, en = 0, tone = 0;
        if(n > 0 && sscanf(buf, "%d %d %d %d", &h, &m, &en, &tone) >= 4 && h >= 0 &&
           h < 24 && m >= 0 && m < 60) {
            alarm_persist.hour = (uint8_t)h;
            alarm_persist.minute = (uint8_t)m;
            alarm_persist.enabled = en ? 1 : 0;
            alarm_persist.tone =
                (tone >= 0 && tone < CLOCK_SETTINGS_TONE_COUNT) ? (uint8_t)tone : 0;
        } else {
            FURI_LOG_W(TAG, "alarm file corrupt, using defaults");
        }
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    DateTime dt = {};
    dt.hour = alarm_persist.hour;
    dt.minute = alarm_persist.minute;
    furi_hal_rtc_set_alarm(&dt, alarm_persist.enabled != 0);

    alarm_loaded = true;
    FURI_LOG_I(
        TAG,
        "alarm loaded: %02u:%02u %s tone=%s",
        alarm_persist.hour,
        alarm_persist.minute,
        alarm_persist.enabled ? "on" : "off",
        alarm_tone_names[alarm_persist.tone]);
}

static void clock_settings_alarm_save(void) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, ALARM_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[48];
        int len = snprintf(
            buf,
            sizeof(buf),
            "%u %u %u %u\n",
            alarm_persist.hour,
            alarm_persist.minute,
            alarm_persist.enabled,
            alarm_persist.tone);
        ok = len > 0 && storage_file_write(file, buf, (size_t)len) == (size_t)len;
        storage_file_close(file);
    }
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    FURI_LOG_I(
        TAG,
        "alarm saved: %02u:%02u %s %s",
        alarm_persist.hour,
        alarm_persist.minute,
        alarm_persist.enabled ? "on" : "off",
        ok ? "" : "(WRITE FAILED)");
}

/* 1 s poller: mirrors RTC alarm <-> file, and rings on exact minute match.
 * The minute key guard fires at most once per matching minute. */
static void clock_settings_alarm_poll(void* context) {
    UNUSED(context);

    if(!alarm_loaded) {
        clock_settings_alarm_load();
        return;
    }

    DateTime cur = {};
    bool enabled = furi_hal_rtc_get_alarm(&cur);
    if(alarm_dirty || cur.hour != alarm_persist.hour ||
       cur.minute != alarm_persist.minute || (enabled ? 1 : 0) != alarm_persist.enabled) {
        alarm_persist.hour = cur.hour;
        alarm_persist.minute = cur.minute;
        alarm_persist.enabled = enabled ? 1 : 0;
        alarm_dirty = false;
        clock_settings_alarm_save();
    }

    if(!alarm_persist.enabled) return;

    DateTime now = {};
    furi_hal_rtc_get_datetime(&now);
    if(now.hour != alarm_persist.hour || now.minute != alarm_persist.minute) return;

    uint32_t key = datetime_datetime_to_timestamp(&now);
    key -= key % 60;
    if(key == alarm_last_fired_key) return;
    alarm_last_fired_key = key;

    FURI_LOG_I(
        TAG,
        "alarm match %02u:%02u -> ringing (tone=%s)",
        now.hour,
        now.minute,
        alarm_tone_names[clock_settings_get_tone()]);
    clock_settings_alarm_isr(NULL);
}

static void clock_settings_alarm_draw_callback(Canvas* canvas, void* ctx) {
    ClockSettingsAlramModel* model = ctx;
    char buffer[64] = {};

    // Clock icon
    canvas_draw_icon_animation(canvas, 5, 6, model->icon);

    // Time
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buffer, sizeof(buffer), "%02u:%02u", model->now.hour, model->now.minute);
    canvas_draw_str(canvas, 58, 32, buffer);

    // Date
    canvas_set_font(canvas, FontPrimary);
    snprintf(
        buffer,
        sizeof(buffer),
        "%02u.%02u.%04u",
        model->now.day,
        model->now.month,
        model->now.year);
    canvas_draw_str(canvas, 60, 44, buffer);

    // Press Back to snooze
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_icon_ex(canvas, 5, 50, &I_Pin_back_arrow_10x8, 0);
    canvas_draw_str_aligned(canvas, 20, 50, AlignLeft, AlignTop, "Snooze");
}

static void clock_settings_alarm_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

void clock_settings_alarm_animation_callback(IconAnimation* instance, void* context) {
    UNUSED(instance);
    ViewPort* view_port = context;
    view_port_update(view_port);
}

int32_t clock_settings_alarm(void* p) {
    UNUSED(p);

    // View Model
    ClockSettingsAlramModel model;
    model.is_snooze = false;

    furi_hal_rtc_get_datetime(&model.now);
    furi_hal_rtc_get_alarm(&model.alarm_start);
    model.icon = icon_animation_alloc(&A_Alarm_47x39);

    // Alloc message queue
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    // Configure view port
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, clock_settings_alarm_draw_callback, &model);
    view_port_input_callback_set(view_port, clock_settings_alarm_input_callback, event_queue);

    // Register view port in GUI
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_set_lockdown_inhibit(gui, true);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    NotificationApp* notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notification, alarm_sequence());

    icon_animation_set_update_callback(
        model.icon, clock_settings_alarm_animation_callback, view_port);
    icon_animation_start(model.icon);

    // Process events
    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 2000) == FuriStatusOk) {
            if(event.type == InputTypePress) {
                // Snooze
                if(event.key == InputKeyBack) {
                    furi_hal_rtc_get_datetime(&model.snooze_until);
                    model.snooze_until.minute += SNOOZE_MINUTES;
                    model.snooze_until.hour += model.snooze_until.minute / 60;
                    model.snooze_until.minute %= 60;
                    model.snooze_until.hour %= 24;

                    model.is_snooze = true;
                    model.alarm_start = model.snooze_until; // For correct timeout behavior
                    view_port_enabled_set(view_port, false);
                    gui_set_lockdown_inhibit(gui, false);
                } else {
                    running = false;
                }
            }
        } else if(model.is_snooze) {
            furi_hal_rtc_get_datetime(&model.now);
            if(datetime_datetime_to_timestamp(&model.now) >=
               datetime_datetime_to_timestamp(&model.snooze_until)) {
                view_port_enabled_set(view_port, true);
                gui_set_lockdown_inhibit(gui, true);

                model.is_snooze = false;
            }
        } else {
            notification_message(notification, alarm_sequence());
            furi_hal_rtc_get_datetime(&model.now);
            view_port_update(view_port);

            // Stop the alarm if it has been ringing for more than TIMEOUT_MINUTES
            if((model.now.hour == model.alarm_start.hour &&
                model.now.minute >= model.alarm_start.minute + TIMEOUT_MINUTES) ||
               (model.now.hour == (model.alarm_start.hour + 1) % 24 &&
                model.now.minute < (model.alarm_start.minute + TIMEOUT_MINUTES) % 60)) {
                running = false;
            }
        }
    }

    icon_animation_stop(model.icon);

    notification_message_block(notification, &sequence_empty);
    furi_record_close(RECORD_NOTIFICATION);

    view_port_enabled_set(view_port, false);
    gui_set_lockdown_inhibit(gui, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);

    icon_animation_free(model.icon);

    return 0;
}

FuriThread* clock_settings_alarm_thread = NULL;

static void clock_settings_alarm_thread_state_callback(
    FuriThread* thread,
    FuriThreadState state,
    void* context) {
    furi_assert(clock_settings_alarm_thread == thread);
    UNUSED(context);

    if(state == FuriThreadStateStopped) {
        furi_thread_free(thread);
        clock_settings_alarm_thread = NULL;
    }
}

static void clock_settings_alarm_start(void* context, uint32_t arg) {
    UNUSED(context);
    UNUSED(arg);

    FURI_LOG_I(TAG, "spawning alarm thread");

    if(clock_settings_alarm_thread) return;

    clock_settings_alarm_thread =
        furi_thread_alloc_ex("ClockAlarm", 2048, clock_settings_alarm, NULL);
    furi_thread_set_state_callback(
        clock_settings_alarm_thread, clock_settings_alarm_thread_state_callback);
    furi_thread_start(clock_settings_alarm_thread);
}

static void clock_settings_alarm_isr(void* context) {
    UNUSED(context);
    furi_timer_pending_callback(clock_settings_alarm_start, NULL, 0);
}

void clock_settings_start(void) {
#ifndef FURI_RAM_EXEC
    furi_hal_rtc_set_alarm_callback(clock_settings_alarm_isr, NULL);
    if(!alarm_poll_timer) {
        alarm_poll_timer =
            furi_timer_alloc(clock_settings_alarm_poll, FuriTimerTypePeriodic, NULL);
        furi_timer_start(alarm_poll_timer, furi_ms_to_ticks(1000));
    }
    FURI_LOG_I(TAG, "alarm poller started (1 s)");
#endif
}

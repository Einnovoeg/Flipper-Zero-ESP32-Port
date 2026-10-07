#include "furi_hal_rtc.h"

#include <time.h>

typedef struct {
    FuriHalRtcHeapTrackMode heap_track_mode;
    FuriHalRtcBootMode boot_mode;
    FuriHalRtcLogDevice log_device;
    FuriHalRtcLogBaudRate log_baud_rate;
    uint8_t log_level;
    uint32_t flags;
    uint32_t fault_data;
    uint32_t pin_fails;
    uint32_t pin_value;
    int64_t time_offset;
    int32_t timezone_offset_min; /* standard-time offset (minutes east) */
    uint8_t timezone_dst_rule; /* FURI_HAL_RTC_TZ_DST_* */
    DateTime alarm;
    bool alarm_enabled;
    FuriHalRtcAlarmCallback alarm_callback;
    void* alarm_context;
    FuriHalRtcLocaleTimeFormat locale_timeformat;
    FuriHalRtcLocaleDateFormat locale_dateformat;
    FuriHalRtcLocaleUnits locale_units;
} FuriHalRtcState;

static FuriHalRtcState furi_hal_rtc = {
    .heap_track_mode = FuriHalRtcHeapTrackModeNone,
    .boot_mode = FuriHalRtcBootModeNormal,
    .log_device = FuriHalRtcLogDeviceNone,
    .log_baud_rate = FuriHalRtcLogBaudRate115200,
    .log_level = 0,
    .flags = 0,
    .fault_data = 0,
    .pin_fails = 0,
    .pin_value = 0,
    .time_offset = 0,
    .timezone_offset_min = 0,
    .timezone_dst_rule = 0,
    .locale_timeformat = FuriHalRtcLocaleTimeFormat24h,
    .locale_dateformat = FuriHalRtcLocaleDateFormatDMY,
    .locale_units = FuriHalRtcLocaleUnitsMetric,
};

static time_t furi_hal_rtc_now(void) {
    return time(NULL) + (time_t)furi_hal_rtc.time_offset;
}

void furi_hal_rtc_init_early(void) {
}

void furi_hal_rtc_deinit_early(void) {
}

void furi_hal_rtc_init(void) {
}

void furi_hal_rtc_prepare_for_shutdown(void) {
}

void furi_hal_rtc_sync_shadow(void) {
}

void furi_hal_rtc_reset_registers(void) {
    furi_hal_rtc.flags = 0;
    furi_hal_rtc.fault_data = 0;
    furi_hal_rtc.pin_fails = 0;
    furi_hal_rtc.pin_value = 0;
    furi_hal_rtc.boot_mode = FuriHalRtcBootModeNormal;
}

void furi_hal_rtc_set_log_level(uint8_t level) {
    furi_hal_rtc.log_level = level;
}

uint8_t furi_hal_rtc_get_log_level(void) {
    return furi_hal_rtc.log_level;
}

void furi_hal_rtc_set_log_device(FuriHalRtcLogDevice device) {
    furi_hal_rtc.log_device = device;
}

FuriHalRtcLogDevice furi_hal_rtc_get_log_device(void) {
    return furi_hal_rtc.log_device;
}

void furi_hal_rtc_set_log_baud_rate(FuriHalRtcLogBaudRate baud_rate) {
    furi_hal_rtc.log_baud_rate = baud_rate;
}

FuriHalRtcLogBaudRate furi_hal_rtc_get_log_baud_rate(void) {
    return furi_hal_rtc.log_baud_rate;
}

void furi_hal_rtc_set_fault_data(uint32_t value) {
    furi_hal_rtc.fault_data = value;
}

uint32_t furi_hal_rtc_get_fault_data(void) {
    return furi_hal_rtc.fault_data;
}

void furi_hal_rtc_set_pin_fails(uint32_t value) {
    furi_hal_rtc.pin_fails = value;
}

uint32_t furi_hal_rtc_get_pin_fails(void) {
    return furi_hal_rtc.pin_fails;
}

void furi_hal_rtc_set_pin_value(uint32_t value) {
    furi_hal_rtc.pin_value = value;
}

uint32_t furi_hal_rtc_get_pin_value(void) {
    return furi_hal_rtc.pin_value;
}

bool furi_hal_rtc_is_flag_set(FuriHalRtcFlag flag) {
    return (furi_hal_rtc.flags & flag) != 0;
}

void furi_hal_rtc_set_flag(FuriHalRtcFlag flag) {
    furi_hal_rtc.flags |= flag;
}

void furi_hal_rtc_reset_flag(FuriHalRtcFlag flag) {
    furi_hal_rtc.flags &= ~flag;
}

FuriHalRtcBootMode furi_hal_rtc_get_boot_mode(void) {
    return furi_hal_rtc.boot_mode;
}

void furi_hal_rtc_set_boot_mode(FuriHalRtcBootMode mode) {
    furi_hal_rtc.boot_mode = mode;
}

FuriHalRtcHeapTrackMode furi_hal_rtc_get_heap_track_mode(void) {
    return furi_hal_rtc.heap_track_mode;
}

void furi_hal_rtc_set_heap_track_mode(FuriHalRtcHeapTrackMode mode) {
    furi_hal_rtc.heap_track_mode = mode;
}

/* ---- timezone / DST ---------------------------------------------------- */

/* Sakamoto: weekday for y/m/d, 0 = Sunday. m is 1..12. */
static int tz_weekday(int y, int m, int d) {
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if(m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int tz_days_in_month(int y, int m) {
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int n = dim[m - 1];
    if(m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) n = 29;
    return n;
}

/* nth (>=1) Sunday of month, or the last Sunday when nth == -1. */
static int tz_sunday_of(int y, int m, int nth) {
    int first_sunday = 1 + ((7 - tz_weekday(y, m, 1)) % 7);
    if(nth > 0) return first_sunday + (nth - 1) * 7;
    int dim = tz_days_in_month(y, m);
    int last_sunday = dim - ((tz_weekday(y, m, dim)) % 7);
    return last_sunday;
}

/* Pack (month,day,hour,min) into one monotonic int for comparisons. */
#define TZ_KEY(mon, day, hour, min) ((mon)*1000000 + (day)*10000 + (hour)*100 + (min))

/* Is `dst_rule` active at UTC instant `utc` for a zone with standard offset
 * `base_min`? Northern-hemisphere rules compare on local-standard time; the
 * Australian rule wraps across the year boundary. */
static bool tz_dst_active(uint8_t dst_rule, time_t utc, int32_t base_min) {
    if(dst_rule == FURI_HAL_RTC_TZ_DST_NONE) return false;

    if(dst_rule == FURI_HAL_RTC_TZ_DST_EU) {
        struct tm tm;
        gmtime_r(&utc, &tm);
        int y = tm.tm_year + 1900;
        int start = TZ_KEY(3, tz_sunday_of(y, 3, -1), 1, 0);
        int end = TZ_KEY(10, tz_sunday_of(y, 10, -1), 1, 0);
        int key = TZ_KEY(tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
        return key >= start && key < end;
    }

    /* US and AU compare on *local standard* time (their transitions are
     * expressed in wall-clock local time). */
    time_t local = utc + (time_t)base_min * 60;
    struct tm tm;
    gmtime_r(&local, &tm);
    int y = tm.tm_year + 1900;
    int key = TZ_KEY(tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);

    if(dst_rule == FURI_HAL_RTC_TZ_DST_US) {
        int start = TZ_KEY(3, tz_sunday_of(y, 3, 2), 2, 0);
        int end = TZ_KEY(11, tz_sunday_of(y, 11, 1), 2, 0);
        return key >= start && key < end;
    }

    if(dst_rule == FURI_HAL_RTC_TZ_DST_AU) {
        int start = TZ_KEY(10, tz_sunday_of(y, 10, 1), 2, 0);
        int end = TZ_KEY(4, tz_sunday_of(y, 4, 1), 3, 0);
        return key >= start || key < end; /* wraps over new year */
    }
    return false;
}

int32_t furi_hal_rtc_tz_effective_offset(int32_t base_minutes, uint8_t dst_rule) {
    return base_minutes + (tz_dst_active(dst_rule, furi_hal_rtc_now(), base_minutes) ? 60 : 0);
}

void furi_hal_rtc_set_timezone_zone(int32_t base_minutes, uint8_t dst_rule) {
    if(base_minutes < -720) base_minutes = -720;
    if(base_minutes > 840) base_minutes = 840;
    furi_hal_rtc.timezone_offset_min = base_minutes;
    furi_hal_rtc.timezone_dst_rule = dst_rule;
}

void furi_hal_rtc_get_timezone_zone(int32_t* base_minutes, uint8_t* dst_rule) {
    if(base_minutes) *base_minutes = furi_hal_rtc.timezone_offset_min;
    if(dst_rule) *dst_rule = furi_hal_rtc.timezone_dst_rule;
}

void furi_hal_rtc_set_timezone_offset(int32_t minutes) {
    /* Plain fixed offset (legacy callers / old /int/.timezone files): no DST. */
    furi_hal_rtc_set_timezone_zone(minutes, FURI_HAL_RTC_TZ_DST_NONE);
}

int32_t furi_hal_rtc_get_timezone_offset(void) {
    return furi_hal_rtc_tz_effective_offset(
        furi_hal_rtc.timezone_offset_min, furi_hal_rtc.timezone_dst_rule);
}

void furi_hal_rtc_set_alarm(const DateTime* datetime, bool enable) {
    if(datetime) {
        furi_hal_rtc.alarm = *datetime;
    }
    furi_hal_rtc.alarm_enabled = enable;
}

bool furi_hal_rtc_get_alarm(DateTime* datetime) {
    if(datetime) {
        *datetime = furi_hal_rtc.alarm;
    }
    return furi_hal_rtc.alarm_enabled;
}

void furi_hal_rtc_set_alarm_callback(FuriHalRtcAlarmCallback callback, void* context) {
    furi_hal_rtc.alarm_callback = callback;
    furi_hal_rtc.alarm_context = context;
}

void furi_hal_rtc_get_datetime(DateTime* datetime) {
    if(!datetime) {
        return;
    }

    time_t now = furi_hal_rtc_now();
    time_t local = now + (time_t)furi_hal_rtc_get_timezone_offset() * 60;
    struct tm now_tm = {0};
    localtime_r(&local, &now_tm);

    datetime->hour = now_tm.tm_hour;
    datetime->minute = now_tm.tm_min;
    datetime->second = now_tm.tm_sec;
    datetime->day = now_tm.tm_mday;
    datetime->month = now_tm.tm_mon + 1;
    datetime->year = now_tm.tm_year + 1900;
    datetime->weekday = ((now_tm.tm_wday + 6) % 7) + 1;
}

void furi_hal_rtc_set_datetime(DateTime* datetime) {
    if(!datetime) {
        return;
    }

    struct tm desired = {
        .tm_sec = datetime->second,
        .tm_min = datetime->minute,
        .tm_hour = datetime->hour,
        .tm_mday = datetime->day,
        .tm_mon = datetime->month - 1,
        .tm_year = datetime->year - 1900,
        .tm_isdst = -1,
    };

    const time_t target = mktime(&desired);
    if(target != (time_t)-1) {
        /* User supplies local wall time: convert back to UTC for storage. */
        furi_hal_rtc.time_offset = (int64_t)target -
                                   (int64_t)furi_hal_rtc_get_timezone_offset() * 60 -
                                   (int64_t)time(NULL);
    }
}

uint32_t furi_hal_rtc_get_timestamp(void) {
    return (uint32_t)furi_hal_rtc_now();
}

FuriHalRtcLocaleTimeFormat furi_hal_rtc_get_locale_timeformat(void) {
    return furi_hal_rtc.locale_timeformat;
}

void furi_hal_rtc_set_locale_timeformat(FuriHalRtcLocaleTimeFormat format) {
    furi_hal_rtc.locale_timeformat = format;
}

FuriHalRtcLocaleDateFormat furi_hal_rtc_get_locale_dateformat(void) {
    return furi_hal_rtc.locale_dateformat;
}

void furi_hal_rtc_set_locale_dateformat(FuriHalRtcLocaleDateFormat format) {
    furi_hal_rtc.locale_dateformat = format;
}

FuriHalRtcLocaleUnits furi_hal_rtc_get_locale_units(void) {
    return furi_hal_rtc.locale_units;
}

void furi_hal_rtc_set_locale_units(FuriHalRtcLocaleUnits format) {
    furi_hal_rtc.locale_units = format;
}

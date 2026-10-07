#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <core/common_defines.h>
#include <datetime/datetime.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FuriHalRtcFlagDebug = (1 << 0),
    FuriHalRtcFlagStorageFormatInternal = (1 << 1),
    FuriHalRtcFlagLock = (1 << 2),
    FuriHalRtcFlagC2Update = (1 << 3),
    FuriHalRtcFlagHandOrient = (1 << 4),
    FuriHalRtcFlagLegacySleep = (1 << 5),
    FuriHalRtcFlagStealthMode = (1 << 6),
    FuriHalRtcFlagDetailedFilename = (1 << 7),
} FuriHalRtcFlag;

typedef enum {
    FuriHalRtcBootModeNormal = 0,
    FuriHalRtcBootModeDfu,
    FuriHalRtcBootModePreUpdate,
    FuriHalRtcBootModeUpdate,
    FuriHalRtcBootModePostUpdate,
} FuriHalRtcBootMode;

typedef enum {
    FuriHalRtcHeapTrackModeNone = 0,
    FuriHalRtcHeapTrackModeMain = 1,
    FuriHalRtcHeapTrackModeTree = 2,
    FuriHalRtcHeapTrackModeAll = 3,
} FuriHalRtcHeapTrackMode;

typedef enum {
    FuriHalRtcLogDeviceUsart = 0x0,
    FuriHalRtcLogDeviceLpuart = 0x1,
    FuriHalRtcLogDeviceReserved = 0x2,
    FuriHalRtcLogDeviceNone = 0x3,
} FuriHalRtcLogDevice;

typedef enum {
    FuriHalRtcLogBaudRate230400 = 0x0,
    FuriHalRtcLogBaudRate9600 = 0x1,
    FuriHalRtcLogBaudRate38400 = 0x2,
    FuriHalRtcLogBaudRate57600 = 0x3,
    FuriHalRtcLogBaudRate115200 = 0x4,
    FuriHalRtcLogBaudRate460800 = 0x5,
    FuriHalRtcLogBaudRate921600 = 0x6,
    FuriHalRtcLogBaudRate1843200 = 0x7,
} FuriHalRtcLogBaudRate;

typedef enum {
    FuriHalRtcLocaleUnitsMetric = 0x0,
    FuriHalRtcLocaleUnitsImperial = 0x1,
} FuriHalRtcLocaleUnits;

typedef enum {
    FuriHalRtcLocaleTimeFormat24h = 0x0,
    FuriHalRtcLocaleTimeFormat12h = 0x1,
} FuriHalRtcLocaleTimeFormat;

typedef enum {
    FuriHalRtcLocaleDateFormatDMY = 0x0,
    FuriHalRtcLocaleDateFormatMDY = 0x1,
    FuriHalRtcLocaleDateFormatYMD = 0x2,
} FuriHalRtcLocaleDateFormat;

void furi_hal_rtc_init_early(void);
void furi_hal_rtc_deinit_early(void);
void furi_hal_rtc_init(void);
void furi_hal_rtc_prepare_for_shutdown(void);
void furi_hal_rtc_sync_shadow(void);
void furi_hal_rtc_reset_registers(void);

void furi_hal_rtc_set_log_level(uint8_t level);
uint8_t furi_hal_rtc_get_log_level(void);
void furi_hal_rtc_set_log_device(FuriHalRtcLogDevice device);
FuriHalRtcLogDevice furi_hal_rtc_get_log_device(void);
void furi_hal_rtc_set_log_baud_rate(FuriHalRtcLogBaudRate baud_rate);
FuriHalRtcLogBaudRate furi_hal_rtc_get_log_baud_rate(void);

void furi_hal_rtc_set_fault_data(uint32_t value);
uint32_t furi_hal_rtc_get_fault_data(void);
void furi_hal_rtc_set_pin_fails(uint32_t value);
uint32_t furi_hal_rtc_get_pin_fails(void);
void furi_hal_rtc_set_pin_value(uint32_t value);
uint32_t furi_hal_rtc_get_pin_value(void);

bool furi_hal_rtc_is_flag_set(FuriHalRtcFlag flag);
void furi_hal_rtc_set_flag(FuriHalRtcFlag flag);
void furi_hal_rtc_reset_flag(FuriHalRtcFlag flag);

FuriHalRtcBootMode furi_hal_rtc_get_boot_mode(void);
void furi_hal_rtc_set_boot_mode(FuriHalRtcBootMode mode);
FuriHalRtcHeapTrackMode furi_hal_rtc_get_heap_track_mode(void);
void furi_hal_rtc_set_heap_track_mode(FuriHalRtcHeapTrackMode mode);

void furi_hal_rtc_get_datetime(DateTime* datetime);
void furi_hal_rtc_set_datetime(DateTime* datetime);
uint32_t furi_hal_rtc_get_timestamp(void);

/** Display timezone offset in minutes east of UTC (e.g. -300 for UTC-5).
 * The system clock itself always stays on UTC (SNTP, file times); the offset
 * is applied symmetrically when converting to/from wall-clock DateTime, so
 * the Clock app, RPC datetime and manual sets all observe local time.
 *
 * These accessors deal with the *effective* offset (standard offset + DST if
 * the zone currently observes it). For zones that observe DST use
 * furi_hal_rtc_set_timezone_zone() to store the standard offset + rule. */
void furi_hal_rtc_set_timezone_offset(int32_t minutes);
int32_t furi_hal_rtc_get_timezone_offset(void);

/** Daylight-saving rules understood by this port (northern + Australian
 * patterns cover every city in the picker). */
#define FURI_HAL_RTC_TZ_DST_NONE 0 /* fixed offset, no DST */
#define FURI_HAL_RTC_TZ_DST_US 1 /* 2nd Sun Mar -> 1st Sun Nov (US/Canada) */
#define FURI_HAL_RTC_TZ_DST_EU 2 /* last Sun Mar -> last Sun Oct (EU/UK) */
#define FURI_HAL_RTC_TZ_DST_AU 3 /* 1st Sun Oct -> 1st Sun Apr (AU, south) */

/** Store a timezone as standard-time offset + DST rule. */
void furi_hal_rtc_set_timezone_zone(int32_t base_minutes, uint8_t dst_rule);
/** Read back the stored standard offset and DST rule. */
void furi_hal_rtc_get_timezone_zone(int32_t* base_minutes, uint8_t* dst_rule);
/** Effective offset (base + DST if active right now) for a given zone — used
 * by the city picker to label each entry with the offset it has today. */
int32_t furi_hal_rtc_tz_effective_offset(int32_t base_minutes, uint8_t dst_rule);

/** Daily alarm (software stub: this port has no VBAT-backed RTC alarm
 * peripheral, so time+enabled live in RAM and nothing fires the callback
 * from hardware; the Clock UI can still set/view the alarm). */
typedef void (*FuriHalRtcAlarmCallback)(void* context);
void furi_hal_rtc_set_alarm(const DateTime* datetime, bool enable);
bool furi_hal_rtc_get_alarm(DateTime* datetime);
void furi_hal_rtc_set_alarm_callback(FuriHalRtcAlarmCallback callback, void* context);

FuriHalRtcLocaleTimeFormat furi_hal_rtc_get_locale_timeformat(void);
void furi_hal_rtc_set_locale_timeformat(FuriHalRtcLocaleTimeFormat format);
FuriHalRtcLocaleDateFormat furi_hal_rtc_get_locale_dateformat(void);
void furi_hal_rtc_set_locale_dateformat(FuriHalRtcLocaleDateFormat format);
FuriHalRtcLocaleUnits furi_hal_rtc_get_locale_units(void);
void furi_hal_rtc_set_locale_units(FuriHalRtcLocaleUnits format);

#ifdef __cplusplus
}
#endif

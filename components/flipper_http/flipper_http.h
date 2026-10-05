#pragma once

/* Native FlipperHTTP compatibility layer for the ESP32 port.
 *
 * jblanked-style apps (FlipWeather, FlipSocial, FlipLibrary, ...) were
 * written against an EXTERNAL ESP32 board reached over UART: every call
 * below used to format a [COMMAND]{...} line for that board's firmware.
 * This port IS the ESP32, so the same API is fulfilled natively (onboard
 * WiFi via wlan_hal, HTTP via esp_http_client, JSON via vendored jsmn).
 *
 * Response contract preserved from the board firmware so app parsers keep
 * working: fhttp.last_response holds the raw body (or an [ERROR]/[PONG]
 * marker), and the registered callback receives the response line by line.
 *
 * Functions that only make sense with an external flashing target
 * (websocket_*, *_esp firmware update, send_command) are present but return
 * false with a log line.
 */

#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/loading.h>
#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_TAG "FlipperHTTP"
#define TIMEOUT_DURATION_TICKS (6 * 1000) /* 6 seconds */
#define BAUDRATE               (115200) /* inert: no UART on native port */
#define UART_CH                (0) /* inert: no UART on native port */
#define RX_BUF_SIZE            1024
#define RX_LINE_BUFFER_SIZE    4096
#define MAX_FILE_SHOW          4096
#define FILE_BUFFER_SIZE       512

typedef void (*FlipperHTTP_Callback)(const char* line, void* context);

typedef enum {
    INACTIVE,
    IDLE,
    RECEIVING,
    SENDING,
    ISSUE,
} SerialState;

typedef enum {
    WorkerEvtStop = (1 << 0),
    WorkerEvtRxDone = (1 << 1),
} WorkerEvtFlags;

typedef struct {
    FuriStreamBuffer* flipper_http_stream;
    void* serial_handle; /* unused natively (kept for layout compat) */
    FuriThread* rx_thread;
    FuriThreadId rx_thread_id;
    FlipperHTTP_Callback handle_rx_line_cb;
    void* callback_context;
    SerialState state;

    char* last_response;
    char file_path[256];

    FuriTimer* get_timeout_timer;
    bool started_receiving_get;
    bool just_started_get;
    bool started_receiving_post;
    bool just_started_post;
    bool started_receiving_put;
    bool just_started_put;
    bool started_receiving_delete;
    bool just_started_delete;

    uint8_t* received_bytes;
    size_t received_bytes_len;
    bool is_bytes_request;
    bool save_bytes;
    bool save_received_data;
    bool just_started_bytes;
} FlipperHTTP;

extern FlipperHTTP fhttp;
extern char rx_line_buffer[RX_LINE_BUFFER_SIZE];
extern uint8_t file_buffer[FILE_BUFFER_SIZE];
extern size_t file_buffer_len;

bool flipper_http_append_to_file(
    const void* data,
    size_t data_size,
    bool start_new_file,
    char* file_path);
FuriString* flipper_http_load_from_file(char* file_path);
int32_t flipper_http_worker(void* context);
void get_timeout_timer_callback(void* context);
void flipper_http_rx_callback(const char* line, void* context);
bool flipper_http_init(FlipperHTTP_Callback callback, void* context);
void flipper_http_deinit(void);
bool flipper_http_send_data(const char* data);
bool flipper_http_ping(void);
bool flipper_http_list_commands(void);
bool flipper_http_led_on(void);
bool flipper_http_led_off(void);
bool flipper_http_parse_json(const char* key, const char* json_data);
bool flipper_http_parse_json_array(const char* key, int index, const char* json_data);
bool flipper_http_scan_wifi(void);
bool flipper_http_save_wifi(const char* ssid, const char* password);
bool flipper_http_ip_address(void);
bool flipper_http_ip_wifi(void);
bool flipper_http_disconnect_wifi(void);
bool flipper_http_connect_wifi(void);
bool flipper_http_get_request(const char* url);
bool flipper_http_get_request_with_headers(const char* url, const char* headers);
bool flipper_http_get_request_bytes(const char* url, const char* headers);
bool flipper_http_post_request_with_headers(
    const char* url,
    const char* headers,
    const char* payload);
bool flipper_http_post_request_bytes(const char* url, const char* headers, const char* payload);
bool flipper_http_put_request_with_headers(
    const char* url,
    const char* headers,
    const char* payload);
bool flipper_http_delete_request_with_headers(
    const char* url,
    const char* headers,
    const char* payload);
char* trim(const char* str);
bool flipper_http_process_response_async(bool (*http_request)(void), bool (*parse_json)(void));
void flipper_http_loading_task(
    bool (*http_request)(void),
    bool (*parse_response)(void),
    uint32_t success_view_id,
    uint32_t failure_view_id,
    ViewDispatcher** view_dispatcher);

/* Native-only extras (absent upstream; safe to ignore if unused). */
bool flipper_http_is_wifi_connected(void);

/* Stubs for external-flasher flows (no second ESP32 on this port). */
bool flipper_http_websocket_start(void);
bool flipper_http_websocket_stop(void);
bool flipper_http_send_command(const char* command);
bool flipper_http_partitions_esp(void);
bool flipper_http_firmware_a_esp(void);
bool flipper_http_bootloader_esp(void);
FlipperHTTP* flipper_http_alloc(void);
void flipper_http_free(FlipperHTTP* http);
bool flipper_http_load_from_file_with_limit(char* file_path, size_t limit);
bool flipper_http_stream(const char* url);

#ifdef __cplusplus
}
#endif

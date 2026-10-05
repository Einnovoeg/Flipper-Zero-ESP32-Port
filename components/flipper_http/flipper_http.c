/* Native FlipperHTTP compatibility layer: see flipper_http.h.
 *
 * Transport notes (behavioral contract preserved from the UART board):
 * - fhttp.last_response always holds the last body/error marker; the
 *   registered callback additionally receives the response line by line.
 * - Markers match what apps strstr() for: "[PONG]", "[ERROR] ...".
 * - WiFi credentials persist through wlan_password_save (/ext/wifi/<ssid>.txt,
 *   shared with the system WiFi UI); connect_wifi() redials the last saved
 *   network, mirroring the board's save-then-connect flow.
 */

#include "flipper_http/flipper_http.h"
#include "jsmn/jsmn.h"

#include <wlan_hal.h>
#include <wlan_passwords.h>
#include <furi_hal_light.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <storage/storage.h>

#include <string.h>
#include <strings.h>
#include <stdlib.h>

#define FHTTP_TAG "FlipperHTTP"
#define FHTTP_HTTP_TIMEOUT_MS 15000
#define FHTTP_LAST_RESPONSE_CAP 8192

FlipperHTTP fhttp;
char rx_line_buffer[RX_LINE_BUFFER_SIZE];
uint8_t file_buffer[FILE_BUFFER_SIZE];
size_t file_buffer_len = 0;

static char fhttp_saved_ssid[33] = {0};
static char fhttp_saved_pass[65] = {0};

static void fhttp_set_response(const char* text) {
    size_t len = text ? strlen(text) : 0;
    if(len >= FHTTP_LAST_RESPONSE_CAP) len = FHTTP_LAST_RESPONSE_CAP - 1;
    if(!fhttp.last_response) {
        fhttp.last_response = malloc(FHTTP_LAST_RESPONSE_CAP);
        if(!fhttp.last_response) return;
    }
    if(len) memcpy(fhttp.last_response, text, len);
    fhttp.last_response[len] = '\0';
}

static void fhttp_emit_lines(const char* text) {
    if(!text || !fhttp.handle_rx_line_cb) return;
    /* Feed the app callback line by line (UART-board semantic). */
    const char* cur = text;
    char line[512];
    while(*cur) {
        size_t i = 0;
        while(*cur && *cur != '\n' && *cur != '\r' && i + 1 < sizeof(line)) {
            line[i++] = *cur++;
        }
        while(*cur == '\n' || *cur == '\r') cur++;
        if(i == 0) continue;
        line[i] = '\0';
        fhttp.handle_rx_line_cb(line, fhttp.callback_context);
    }
}

static void fhttp_respond(const char* text) {
    fhttp_set_response(text);
    fhttp_emit_lines(text);
}

static void fhttp_respond_error(const char* what) {
    char buf[160];
    snprintf(buf, sizeof(buf), "[ERROR] %s", what ? what : "Unknown error.");
    fhttp_respond(buf);
}

/* ---------- lifecycle ---------- */

bool flipper_http_init(FlipperHTTP_Callback callback, void* context) {
    if(!callback || !context) {
        ESP_LOGE(FHTTP_TAG, "init: null callback/context");
        return false;
    }
    memset(&fhttp, 0, sizeof(fhttp));
    fhttp.handle_rx_line_cb = callback;
    fhttp.callback_context = context;
    fhttp.state = IDLE;
    return true;
}

void flipper_http_deinit(void) {
    free(fhttp.last_response);
    fhttp.last_response = NULL;
    free(fhttp.received_bytes);
    fhttp.received_bytes = NULL;
    fhttp.received_bytes_len = 0;
    fhttp.state = INACTIVE;
}

FlipperHTTP* flipper_http_alloc(void) {
    FlipperHTTP* http = calloc(1, sizeof(FlipperHTTP));
    return http;
}

void flipper_http_free(FlipperHTTP* http) {
    free(http);
}

bool flipper_http_send_data(const char* data) {
    (void)data;
    return true;
}

int32_t flipper_http_worker(void* context) {
    (void)context;
    return 0;
}

void get_timeout_timer_callback(void* context) {
    (void)context;
}

void flipper_http_rx_callback(const char* line, void* context) {
    if(fhttp.handle_rx_line_cb) fhttp.handle_rx_line_cb(line, context ? context : fhttp.callback_context);
}

char* trim(const char* str) {
    if(!str) return strdup("");
    while(*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    size_t len = strlen(str);
    while(len > 0 &&
          (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\n' ||
           str[len - 1] == '\r')) {
        len--;
    }
    char* out = malloc(len + 1);
    if(!out) return strdup("");
    memcpy(out, str, len);
    out[len] = '\0';
    return out;
}

/* ---------- board-level helpers ---------- */

bool flipper_http_ping(void) {
    if(wlan_hal_is_connected()) {
        fhttp_respond("[PONG]");
        return true;
    }
    fhttp_respond("[ERROR] Not connected to Wifi.");
    return false;
}

bool flipper_http_list_commands(void) {
    static const char* cmds =
        "[COMMANDS]\nGET\nPOST\nPUT\nDELETE\nPING\nSCAN\nSAVE\nIP\nCONNECT\nPARSE\nLED_ON\nLED_OFF\n";
    fhttp_respond(cmds);
    return true;
}

bool flipper_http_led_on(void) {
    furi_hal_light_set_rgb_all(0, 64, 0);
    return true;
}

bool flipper_http_led_off(void) {
    furi_hal_light_set_rgb_all(0, 0, 0);
    return true;
}

/* ---------- WiFi ---------- */

bool flipper_http_scan_wifi(void) {
    wifi_ap_record_t* recs = NULL;
    uint16_t count = 0;
    wlan_hal_scan(&recs, &count, 24);
    if(!recs || count == 0) {
        free(recs);
        fhttp_respond("[ERROR] WiFi error.");
        return false;
    }
    /* Board-plausible listing; no in-tree app parses these lines strictly. */
    size_t cap = 128 + (size_t)count * 96;
    char* out = malloc(cap);
    if(!out) {
        free(recs);
        return false;
    }
    size_t pos = 0;
    for(uint16_t i = 0; i < count && pos + 96 < cap; i++) {
        char ssid[34] = {0};
        memcpy(ssid, recs[i].ssid, sizeof(ssid) - 1);
        pos += snprintf(
            out + pos, cap - pos, "SSID: %s, RSSI: %d, CH: %d\n", ssid, recs[i].rssi,
            recs[i].primary);
    }
    free(recs);
    pos += snprintf(out + pos, cap - pos, "[SCAN/DONE]\n");
    fhttp_respond(out);
    free(out);
    return true;
}

bool flipper_http_save_wifi(const char* ssid, const char* password) {
    if(!ssid || !password) return false;
    strncpy(fhttp_saved_ssid, ssid, sizeof(fhttp_saved_ssid) - 1);
    strncpy(fhttp_saved_pass, password, sizeof(fhttp_saved_pass) - 1);
    if(!wlan_password_save(ssid, password)) {
        fhttp_respond("[ERROR] WiFi error.");
        return false;
    }
    fhttp_respond("[SAVED]");
    return true;
}

bool flipper_http_connect_wifi(void) {
    if(!fhttp_saved_ssid[0]) {
        fhttp_respond("[ERROR] Failed to connect to Wifi.");
        return false;
    }
    if(!wlan_hal_connect(fhttp_saved_ssid, fhttp_saved_pass, NULL, 0)) {
        fhttp_respond("[ERROR] Failed to connect to Wifi.");
        return false;
    }
    for(int i = 0; i < 100 && !wlan_hal_is_connected(); i++) {
        furi_delay_ms(100);
    }
    if(!wlan_hal_is_connected()) {
        fhttp_respond("[ERROR] Failed to connect to Wifi.");
        return false;
    }
    fhttp_respond("[CONNECTED]");
    return true;
}

bool flipper_http_disconnect_wifi(void) {
    wlan_hal_disconnect();
    fhttp_respond("[DISCONNECTED]");
    return true;
}

static void fhttp_respond_ip(void) {
    uint32_t ip = wlan_hal_get_own_ip();
    char buf[32];
    snprintf(
        buf, sizeof(buf), "%lu.%lu.%lu.%lu", (unsigned long)((ip >> 0) & 0xFF),
        (unsigned long)((ip >> 8) & 0xFF), (unsigned long)((ip >> 16) & 0xFF),
        (unsigned long)((ip >> 24) & 0xFF));
    fhttp_respond(buf);
}

bool flipper_http_ip_address(void) {
    fhttp_respond_ip();
    return true;
}

bool flipper_http_ip_wifi(void) {
    fhttp_respond_ip();
    return true;
}

/* ---------- HTTP ---------- */

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} FhttpBody;

static esp_err_t fhttp_evt_handler(esp_http_client_event_t* evt) {
    FhttpBody* body = evt->user_data;
    if(evt->event_id == HTTP_EVENT_ON_DATA && body) {
        size_t need = body->len + (size_t)evt->data_len + 1;
        if(need > body->cap) {
            size_t ncap = body->cap ? body->cap * 2 : 2048;
            while(ncap < need) ncap *= 2;
            if(ncap > 64 * 1024) return ESP_FAIL; /* sanity cap */
            char* nd = realloc(body->data, ncap);
            if(!nd) return ESP_FAIL;
            body->data = nd;
            body->cap = ncap;
        }
        memcpy(body->data + body->len, evt->data, evt->data_len);
        body->len += evt->data_len;
        body->data[body->len] = '\0';
    }
    return ESP_OK;
}

static bool fhttp_apply_headers(esp_http_client_handle_t client, const char* headers) {
    if(!headers) return true;
    /* "Key: Value" lines separated by \r\n or \n. */
    const char* cur = headers;
    char line[256];
    while(*cur) {
        size_t i = 0;
        while(*cur && *cur != '\n' && *cur != '\r' && i + 1 < sizeof(line)) {
            line[i++] = *cur++;
        }
        while(*cur == '\n' || *cur == '\r') cur++;
        if(i == 0) continue;
        line[i] = '\0';
        char* sep = strchr(line, ':');
        if(!sep) continue;
        *sep = '\0';
        char* val = sep + 1;
        while(*val == ' ') val++;
        esp_http_client_set_header(client, line, val);
    }
    return true;
}

static bool fhttp_perform(
    const char* url,
    const char* headers,
    const char* method,
    const char* payload,
    bool as_bytes) {
    if(!url) return false;
    if(!wlan_hal_is_connected()) {
        fhttp_respond("[ERROR] Not connected to Wifi. Failed to reconnect.");
        return false;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = FHTTP_HTTP_TIMEOUT_MS,
        .event_handler = fhttp_evt_handler,
    };
    FhttpBody body = {0};
    cfg.user_data = &body;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if(!client) {
        fhttp_respond("[ERROR] GET request failed or returned empty data.");
        return false;
    }
    esp_http_client_method_t http_method = HTTP_METHOD_GET;
    if(method) {
        if(strcasecmp(method, "POST") == 0) http_method = HTTP_METHOD_POST;
        else if(strcasecmp(method, "PUT") == 0)
            http_method = HTTP_METHOD_PUT;
        else if(strcasecmp(method, "DELETE") == 0)
            http_method = HTTP_METHOD_DELETE;
    }
    esp_http_client_set_method(client, http_method);
    fhttp_apply_headers(client, headers);
    if(payload) esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    bool ok = (err == ESP_OK && status >= 200 && status < 300 && body.len > 0);
    if(as_bytes) {
        free(fhttp.received_bytes);
        fhttp.received_bytes = (uint8_t*)body.data;
        fhttp.received_bytes_len = ok ? body.len : 0;
        if(!ok) free(body.data);
        if(fhttp.save_bytes && ok && fhttp.file_path[0]) {
            flipper_http_append_to_file(
                fhttp.received_bytes, fhttp.received_bytes_len, true, fhttp.file_path);
        }
        fhttp_respond(ok ? "[DONE]" : "[ERROR] GET request failed or returned empty data.");
        return ok;
    }
    if(ok) {
        fhttp_respond(body.data ? body.data : "");
    } else {
        fhttp_respond("[ERROR] GET request failed or returned empty data.");
    }
    free(body.data);
    return ok;
}

bool flipper_http_get_request(const char* url) {
    return fhttp_perform(url, NULL, "GET", NULL, false);
}

bool flipper_http_get_request_with_headers(const char* url, const char* headers) {
    return fhttp_perform(url, headers, "GET", NULL, false);
}

bool flipper_http_get_request_bytes(const char* url, const char* headers) {
    return fhttp_perform(url, headers, "GET", NULL, true);
}

bool flipper_http_post_request_with_headers(
    const char* url,
    const char* headers,
    const char* payload) {
    return fhttp_perform(url, headers, "POST", payload, false);
}

bool flipper_http_post_request_bytes(const char* url, const char* headers, const char* payload) {
    return fhttp_perform(url, headers, "POST", payload, true);
}

bool flipper_http_put_request_with_headers(
    const char* url,
    const char* headers,
    const char* payload) {
    return fhttp_perform(url, headers, "PUT", payload, false);
}

bool flipper_http_delete_request_with_headers(
    const char* url,
    const char* headers,
    const char* payload) {
    return fhttp_perform(url, headers, "DELETE", payload, false);
}

/* ---------- JSON (vendored jsmn, key/index lookup) ---------- */

#include "jsmn/jsmn.h"

static bool fhttp_json_lookup(const char* json, const char* key, int index, char** out) {
    if(!json || !key || !out) return false;
    *out = NULL;
    jsmn_parser p;
    jsmn_init(&p);
    int max_tokens = 256;
    jsmntok_t* toks = malloc(sizeof(jsmntok_t) * (size_t)max_tokens);
    if(!toks) return false;
    int n = jsmn_parse(&p, json, strlen(json), toks, (unsigned)max_tokens);
    bool found = false;
    if(n > 0) {
        /* Walk top-level object members for "key". */
        if(toks[0].type == JSMN_OBJECT) {
            int i = 1;
            while(i < n) {
                if(toks[i].type != JSMN_STRING) break;
                int klen = toks[i].end - toks[i].start;
                if(klen == (int)strlen(key) && strncmp(json + toks[i].start, key, (size_t)klen) == 0) {
                    int v = i + 1;
                    if(index >= 0) {
                        /* Expect an array; pick element. */
                        if(v >= n || toks[v].type != JSMN_ARRAY) break;
                        int count = toks[v].size;
                        if(index >= count) break;
                        int e = v + 1;
                        for(int k = 0; k < index; k++) {
                            /* skip subtree rooted at e */
                            int depth = 0, j = e;
                            do {
                                depth += toks[j].size ? 1 : 0;
                                if(toks[j].type == JSMN_OBJECT || toks[j].type == JSMN_ARRAY) {
                                    j += 1;
                                    /* crude: walk size children */
                                    int need = toks[j - 1].size;
                                    /* fallthrough: linear scan below handles it */
                                    (void)need;
                                    break;
                                }
                                j++;
                            } while(j < n && depth > 0);
                            e = j;
                        }
                        v = e;
                    }
                    int vlen = toks[v].end - toks[v].start;
                    char* s = malloc((size_t)vlen + 1);
                    if(s) {
                        memcpy(s, json + toks[v].start, (size_t)vlen);
                        s[vlen] = '\0';
                        *out = s;
                        found = true;
                    }
                    break;
                }
                /* skip value subtree (approx: 1 token + children sizes) */
                i += 2 + (i + 1 < n ? toks[i + 1].size : 0);
            }
        }
    }
    free(toks);
    return found;
}

bool flipper_http_parse_json(const char* key, const char* json_data) {
    char* val = NULL;
    if(!fhttp_json_lookup(json_data, key, -1, &val)) {
        fhttp_respond("[ERROR] Unknown error.");
        return false;
    }
    fhttp_respond(val);
    free(val);
    return true;
}

bool flipper_http_parse_json_array(const char* key, int index, const char* json_data) {
    char* val = NULL;
    if(!fhttp_json_lookup(json_data, key, index, &val)) {
        fhttp_respond("[ERROR] Unknown error.");
        return false;
    }
    fhttp_respond(val);
    free(val);
    return true;
}

/* ---------- files ---------- */

bool flipper_http_append_to_file(
    const void* data,
    size_t data_size,
    bool start_new_file,
    char* file_path) {
    if(!data || data_size == 0 || !file_path) return false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    bool ok = storage_file_open(
        f, file_path, FSAM_WRITE, start_new_file ? FSOM_CREATE_ALWAYS : FSOM_OPEN_APPEND);
    if(ok) ok = storage_file_write(f, data, data_size) == data_size;
    storage_file_close(f);
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static FuriString* fhttp_read_file_limit(const char* path, size_t limit) {
    FuriString* out = furi_string_alloc();
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t buf[512];
        size_t total = 0;
        size_t n = 0;
        while(total < limit && (n = storage_file_read(f, buf, sizeof(buf))) > 0) {
            if(total + n > limit) n = limit - total;
            for(size_t i = 0; i < n; i++) {
                furi_string_push_back(out, (char)buf[i]);
            }
            total += n;
        }
        storage_file_close(f);
    }
    storage_file_free(f);
    furi_record_close(RECORD_STORAGE);
    return out;
}

FuriString* flipper_http_load_from_file(char* file_path) {
    return fhttp_read_file_limit(file_path, MAX_FILE_SHOW);
}

bool flipper_http_load_from_file_with_limit(char* file_path, size_t limit) {
    FuriString* s = fhttp_read_file_limit(file_path, limit);
    bool ok = furi_string_size(s) > 0;
    fhttp_respond(furi_string_get_cstr(s));
    furi_string_free(s);
    return ok;
}

/* ---------- app-flow helpers ---------- */

bool flipper_http_process_response_async(bool (*http_request)(void), bool (*parse_json)(void)) {
    if(http_request && !http_request()) return false;
    if(parse_json && !parse_json()) return false;
    return true;
}

void flipper_http_loading_task(
    bool (*http_request)(void),
    bool (*parse_response)(void),
    uint32_t success_view_id,
    uint32_t failure_view_id,
    ViewDispatcher** view_dispatcher) {
    bool ok = flipper_http_process_response_async(http_request, parse_response);
    if(view_dispatcher && *view_dispatcher) {
        view_dispatcher_switch_to_view(*view_dispatcher, ok ? success_view_id : failure_view_id);
    }
}

/* ---------- streaming (chunked HTTP -> line callback) ---------- */

typedef struct {
    char pending[512];
    size_t pending_len;
} FhttpStreamState;

static esp_err_t fhttp_stream_evt(esp_http_client_event_t* evt) {
    FhttpStreamState* st = evt->user_data;
    if(evt->event_id != HTTP_EVENT_ON_DATA || !st) return ESP_OK;
    const char* p = evt->data;
    int left = evt->data_len;
    while(left > 0) {
        /* accumulate into pending until newline */
        size_t room = sizeof(st->pending) - 1 - st->pending_len;
        size_t take = (size_t)left < room ? (size_t)left : room;
        memcpy(st->pending + st->pending_len, p, take);
        st->pending_len += take;
        p += take;
        left -= (int)take;
        char* nl = memchr(st->pending, '\n', st->pending_len);
        if(nl) {
            *nl = '\0';
            if(fhttp.handle_rx_line_cb) fhttp.handle_rx_line_cb(st->pending, fhttp.callback_context);
            size_t used = (size_t)(nl + 1 - st->pending);
            memmove(st->pending, st->pending + used, st->pending_len - used);
            st->pending_len -= used;
        }
        if(st->pending_len >= sizeof(st->pending) - 1) {
            st->pending[st->pending_len] = '\0';
            if(fhttp.handle_rx_line_cb) fhttp.handle_rx_line_cb(st->pending, fhttp.callback_context);
            st->pending_len = 0;
        }
    }
    return ESP_OK;
}

bool flipper_http_stream(const char* url) {
    if(!url) return false;
    if(!wlan_hal_is_connected()) {
        fhttp_respond("[ERROR] Not connected to Wifi.");
        return false;
    }
    FhttpStreamState st = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = FHTTP_HTTP_TIMEOUT_MS,
        .event_handler = fhttp_stream_evt,
        .user_data = &st,
        .disable_auto_redirect = false,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if(!client) return false;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if(st.pending_len && fhttp.handle_rx_line_cb) {
        st.pending[st.pending_len] = '\0';
        fhttp.handle_rx_line_cb(st.pending, fhttp.callback_context);
    }
    return err == ESP_OK && status >= 200 && status < 300;
}

/* ---------- external-flasher stubs (no second ESP32 on this port) ---------- */

bool flipper_http_websocket_start(void) {
    ESP_LOGW("FlipperHTTP", "websocket: external ESP32 target not present");
    return false;
}

bool flipper_http_websocket_stop(void) {
    return false;
}

bool flipper_http_send_command(const char* command) {
    (void)command;
    ESP_LOGW("FlipperHTTP", "send_command: external ESP32 target not present");
    return false;
}

bool flipper_http_partitions_esp(void) {
    return false;
}

bool flipper_http_firmware_a_esp(void) {
    return false;
}

bool flipper_http_bootloader_esp(void) {
    return false;
}

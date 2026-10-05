#include "radio_stream.h"
#include "radio.h"
#include "radio_view.h"
#include "../streaming/mp3_sink.h"
#include "../streaming/lib/helix/mp3dec.h"

#include <furi.h>
#include <furi_hal.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include <string.h>
#include <stdlib.h>

#define TAG "RadioStream"
#define RADIO_HTTP_TIMEOUT_MS 15000
#define RADIO_READ_BUF        4096
#define RADIO_PCM_FRAMES      1152

typedef struct {
    RadioApp* app;
    /* ICY metadata parsing. */
    int icy_interval; /* 0 = no metadata */
    int icy_countdown;
    uint8_t meta_len; /* remaining metadata bytes (x16) */
    uint8_t meta_buf[512];
    size_t meta_pos;
} RadioStream;

/* Feed raw stream bytes through the ICY metadata filter. Returns the number
 * of AUDIO bytes placed at out (<= len). Updates track title on metadata.
 * Invariant: icy_countdown = audio bytes remaining before the next length
 * byte; meta_len = metadata bytes still to consume. */
static size_t radio_icy_filter(RadioStream* st, const uint8_t* in, size_t len, uint8_t* out) {
    size_t produced = 0;
    size_t i = 0;
    while(i < len) {
        if(st->icy_interval <= 0) {
            /* No metadata: passthrough. */
            size_t avail = len - i;
            memcpy(out + produced, in + i, avail);
            produced += avail;
            break;
        }
        if(st->meta_len > 0) {
            /* Inside a metadata block. */
            size_t take = st->meta_len;
            if(take > len - i) take = len - i;
            size_t room = sizeof(st->meta_buf) - 1 - st->meta_pos;
            if(take > room) take = room;
            memcpy(st->meta_buf + st->meta_pos, in + i, take);
            st->meta_pos += take;
            i += take;
            st->meta_len -= (uint8_t)take;
            if(st->meta_len == 0) {
                st->meta_buf[st->meta_pos] = '\0';
                /* StreamTitle='...'; */
                const char* p = strstr((const char*)st->meta_buf, "StreamTitle='");
                if(p) {
                    p += 13;
                    const char* e = strchr(p, '\'');
                    size_t n = e ? (size_t)(e - p) : 0;
                    if(n > 0) {
                        char title[RADIO_TITLE_LEN];
                        if(n >= sizeof(title)) n = sizeof(title) - 1;
                        memcpy(title, p, n);
                        title[n] = '\0';
                        radio_view_set_track(st->app->player_view, title);
                    }
                }
                st->meta_pos = 0;
            }
            continue;
        }
        if(st->icy_countdown == 0) {
            /* Length byte (units of 16). */
            st->meta_len = (uint8_t)(in[i] * 16);
            i++;
            st->icy_countdown = st->icy_interval;
            st->meta_pos = 0;
            continue;
        }
        size_t avail = len - i;
        size_t chunk = (size_t)st->icy_countdown;
        if(chunk > avail) chunk = avail;
        memcpy(out + produced, in + i, chunk);
        produced += chunk;
        i += chunk;
        st->icy_countdown -= (int)chunk;
    }
    return produced;
}

bool radio_stream_play(RadioApp* app, const char* url) {
    if(!app || !url) return false;

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = RADIO_HTTP_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if(!client) return false;
    esp_http_client_set_header(client, "Icy-MetaData", "1");
    esp_http_client_set_header(client, "User-Agent", "FlipperZero-Radio/1.0");

    if(esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }
    int status = esp_http_client_get_status_code(client);
    int64_t content_len = esp_http_client_get_content_length(client);
    (void)content_len;
    if(status < 200 || status >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    /* ICY metadata interval from response headers. */
    RadioStream st = {0};
    st.app = app;
    char interval_buf[16] = {0};
    if(esp_http_client_get_header(client, "icy-metaint", interval_buf, sizeof(interval_buf)) ==
       ESP_OK) {
        st.icy_interval = atoi(interval_buf);
        st.icy_countdown = st.icy_interval;
        FURI_LOG_I(TAG, "ICY metadata every %d bytes", st.icy_interval);
    }

    HMP3Decoder hmp3 = MP3InitDecoder();
    if(!hmp3) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    static uint8_t net_buf[RADIO_READ_BUF];
    static uint8_t audio_buf[RADIO_READ_BUF + 512];
    static uint8_t frame_buf[RADIO_READ_BUF * 2];
    size_t frame_left = 0;
    static int16_t pcm[RADIO_PCM_FRAMES * 2];
    uint32_t rate = 0;
    uint32_t elapsed_sec = 0;
    uint32_t frames_played = 0;
    bool played_anything = false;

    mp3_sink_init_speaker(44100);
    mp3_sink_set_volume(app->volume);
    radio_view_set_status(app->player_view, 0, app->volume, "Buffering...");

    while(app->worker_run) {
        int n = esp_http_client_read(client, (char*)net_buf, sizeof(net_buf));
        if(n < 0) {
            FURI_LOG_W(TAG, "stream read error");
            break;
        }
        if(n == 0) {
            /* End of stream. */
            break;
        }
        size_t audio_len = radio_icy_filter(&st, net_buf, (size_t)n, audio_buf);
        if(frame_left + audio_len > sizeof(frame_buf)) {
            /* Overrun: drop oldest to stay live. */
            size_t drop = frame_left + audio_len - sizeof(frame_buf);
            memmove(frame_buf, frame_buf + drop, frame_left - drop);
            frame_left -= drop;
        }
        memcpy(frame_buf + frame_left, audio_buf, audio_len);
        frame_left += audio_len;

        /* Decode all complete frames currently buffered. */
        while(app->worker_run && frame_left > 4) {
            int sync = MP3FindSyncWord(frame_buf, (int)frame_left);
            if(sync < 0) {
                frame_left = 0;
                break;
            }
            if(sync > 0) {
                memmove(frame_buf, frame_buf + sync, frame_left - (size_t)sync);
                frame_left -= (size_t)sync;
            }
            unsigned char* in_ptr = frame_buf;
            int bytes_left = (int)frame_left;
            int err = MP3Decode(hmp3, &in_ptr, &bytes_left, pcm, 0);
            if(err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
                break; /* need more bytes */
            }
            if(err != ERR_MP3_NONE) {
                /* Corrupt frame: skip a byte. */
                memmove(frame_buf, frame_buf + 1, frame_left - 1);
                frame_left--;
                continue;
            }
            size_t consumed = (size_t)(in_ptr - frame_buf);
            memmove(frame_buf, in_ptr, frame_left - consumed);
            frame_left -= consumed;

            MP3FrameInfo fi;
            MP3GetLastFrameInfo(hmp3, &fi);
            if(rate == 0 || fi.samprate != (int)rate) {
                rate = (uint32_t)fi.samprate;
                mp3_sink_set_sample_rate(rate);
                FURI_LOG_I(TAG, "stream: %lu Hz %d ch", (unsigned long)rate, fi.nChans);
            }
            size_t frames = (size_t)fi.outputSamps / (size_t)fi.nChans;
            if(frames > RADIO_PCM_FRAMES) frames = RADIO_PCM_FRAMES;
            if(fi.nChans == 1) {
                for(int i = (int)frames - 1; i >= 0; i--) {
                    pcm[i * 2 + 0] = pcm[i];
                    pcm[i * 2 + 1] = pcm[i];
                }
            }
            mp3_sink_push(pcm, frames, 1000);
            played_anything = true;
            frames_played += (uint32_t)frames;
            if(rate) {
                uint32_t sec = frames_played / rate;
                if(sec != elapsed_sec) {
                    elapsed_sec = sec;
                    radio_view_set_status(app->player_view, elapsed_sec, app->volume, "Playing");
                }
            }
        }
    }

    mp3_sink_flush();
    mp3_sink_deinit();
    MP3FreeDecoder(hmp3);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return played_anything;
}

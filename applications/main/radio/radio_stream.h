#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct RadioApp RadioApp;

/* Blocking stream worker: connects, decodes MP3 frames and pushes PCM until
 * stopped, error, or stream end. Runs on its own thread; reports progress
 * through the player view. Returns true when it played anything. */
bool radio_stream_play(RadioApp* app, const char* url);

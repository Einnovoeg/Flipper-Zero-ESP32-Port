#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>

#include "voice_memo_view.h"

#define VOICE_MEMO_DIR_NAME  "voice_memos"
#define VOICE_MEMO_MAX_FILES 64
#define VOICE_MEMO_NAME_LEN  32
#define VOICE_MEMO_PATH_LEN  96

typedef enum {
    VoiceMemoViewSubmenu,
    VoiceMemoViewRecord,
    VoiceMemoViewCount,
} VoiceMemoView;

/** Custom events from views (submenu selections carry the file index). */
typedef enum {
    VoiceMemoEventRecordNew = 100,
    VoiceMemoEventPlayFile,
    VoiceMemoEventStop,
} VoiceMemoEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* submenu;
    VoiceMemoRecordView* vm_view;
    FuriThread* worker;
    volatile bool worker_run;
    char dir_path[VOICE_MEMO_PATH_LEN];
    char file_path[VOICE_MEMO_PATH_LEN];
    char file_names[VOICE_MEMO_MAX_FILES][VOICE_MEMO_NAME_LEN];
    uint8_t file_count;
    char header[48];
} VoiceMemoApp;

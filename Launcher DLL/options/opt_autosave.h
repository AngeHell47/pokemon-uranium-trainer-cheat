#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

enum OptAutosaveStatus {
    OPT_AUTOSAVE_DISABLED = 0,
    OPT_AUTOSAVE_COUNTDOWN = 1,
    OPT_AUTOSAVE_WAITING_SAFE = 2,
    OPT_AUTOSAVE_SAVING = 3,
    OPT_AUTOSAVE_SAVED = 4,
    OPT_AUTOSAVE_LOADING = 5,
    OPT_AUTOSAVE_LOADED = 6,
    OPT_AUTOSAVE_ERROR = -1,
    OPT_AUTOSAVE_LOAD_ERROR = -2,
    OPT_AUTOSAVE_LOAD_REFUSED = -3
};

struct OptAutosaveSlot {
    bool valid;
    LONG timestamp;
    LONG size_bytes;
};

extern bool g_autosave_enabled;

bool opt_autosave_init(const char* ini_path);
void opt_autosave_shutdown();
void opt_autosave_toggle(bool enabled);
void opt_autosave_request_refresh();
void opt_autosave_request_load(int physical_slot);
OptAutosaveSlot opt_autosave_slot(int physical_slot);
LONG opt_autosave_status();
LONG opt_autosave_revision();
int opt_autosave_seconds_until_next();

#pragma once
#include <windows.h>

extern bool g_auto_load_save;
extern bool g_fast_boot;

void opt_startup_init(const char* ini_path);
// These preferences apply on the next game launch.  The auto-trainer helper
// also installs the embedded version.dll proxy beside Uranium.exe.
bool opt_startup_set_auto_trainer(bool enabled);
void opt_startup_set_fast_boot(bool enabled);
bool opt_startup_auto_trainer_enabled();
bool opt_startup_fast_boot_enabled();
// Absolute trainer.ini path owned by the game process directory.
const char* opt_startup_config_path();
const char* opt_startup_last_error();
void opt_startup_set_hwnd_and_start(HWND hwnd);
bool opt_startup_wait_for_game(DWORD timeout_ms);

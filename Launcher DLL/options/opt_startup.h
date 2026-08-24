#pragma once
#include <windows.h>

extern bool g_auto_load_save;
extern bool g_fast_boot;

void opt_startup_init(const char* ini_path);
void opt_startup_set_fast_boot(bool enabled);
bool opt_startup_fast_boot_enabled();
// Absolute trainer.ini path owned by the game process directory.
const char* opt_startup_config_path();
void opt_startup_set_hwnd_and_start(HWND hwnd);
bool opt_startup_wait_for_game(DWORD timeout_ms);

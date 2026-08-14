#pragma once
#include <windows.h>

extern bool g_auto_load_save;

void opt_startup_init(const char* ini_path);
void opt_startup_set_hwnd_and_start(HWND hwnd);
bool opt_startup_wait_for_game(DWORD timeout_ms);

#pragma once
#include <windows.h>

extern bool g_noenc;

void opt_noenc_init(const char* ini_path);
void opt_noenc_set_hwnd_and_start(HWND hwnd);
void opt_noenc_toggle(bool enabled);

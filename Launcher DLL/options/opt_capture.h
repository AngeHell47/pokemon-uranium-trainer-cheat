#pragma once
#include <windows.h>

extern bool g_capture_guaranteed;

void opt_capture_init(const char* ini_path);
void opt_capture_set_hwnd_and_start(HWND hwnd);
void opt_capture_toggle(bool enabled);

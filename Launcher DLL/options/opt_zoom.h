#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern int g_zoom_value;  // 100..300 (100=normal, 200=voit 2x plus)

void opt_zoom_init(const char* ini_path);
void opt_zoom_set_hwnd_and_start(HWND hwnd);
void opt_zoom_apply(int percent);
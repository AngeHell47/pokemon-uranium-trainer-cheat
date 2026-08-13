#pragma once
#include <windows.h>

extern bool g_noclip;

void opt_noclip_init(const char* ini_path);
void opt_noclip_set_hwnd_and_start(HWND hwnd);
void opt_noclip_toggle(bool enabled);

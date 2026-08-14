#pragma once
#include <windows.h>

extern bool g_hm_forget_enabled;

void opt_hmforget_init(const char* ini_path);
void opt_hmforget_set_hwnd_and_start(HWND hwnd);
void opt_hmforget_toggle(bool enabled);

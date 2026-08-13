#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern bool g_pp_lock;

void opt_pp_init(const char* ini_path);
void opt_pp_set_hwnd_and_start(HWND hwnd);
void opt_pp_toggle(bool enabled);
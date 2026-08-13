#pragma once
#include <windows.h>

extern bool g_ohk_lock;

void opt_ohk_init(const char* ini_path);
void opt_ohk_set_hwnd_and_start(HWND hwnd);
void opt_ohk_toggle(bool enabled);

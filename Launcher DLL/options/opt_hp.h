#pragma once
#include <windows.h>

extern bool g_hp_lock;

void opt_hp_init(const char* ini_path);
void opt_hp_set_hwnd(HWND hwnd);
void opt_hp_set_hwnd_and_start(HWND hwnd);  // appeler apres FindWindow
void opt_hp_toggle(bool enabled);

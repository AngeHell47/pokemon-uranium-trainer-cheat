#pragma once
#include <windows.h>

extern bool g_egg_hatch_instant;

void opt_egghatch_init(const char* ini_path);
void opt_egghatch_set_hwnd_and_start(HWND hwnd);
void opt_egghatch_toggle(bool enabled);

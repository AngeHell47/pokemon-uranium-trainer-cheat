#pragma once

#include <windows.h>

extern int g_damage_multiplier;

void opt_damage_init(const char* ini_path);
void opt_damage_set_hwnd_and_start(HWND hwnd);
void opt_damage_apply(int multiplier);

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern bool g_speedhack_enabled;
extern int  g_speedhack_value10; // 0..100 => 0.0..10.0

void opt_speedhack_init(const char* ini_path);
void opt_speedhack_set_hwnd_and_start(HWND hwnd);
void opt_speedhack_toggle(bool enabled);
void opt_speedhack_apply(int value10);
#pragma once
#include <windows.h>

extern bool g_time_enabled;
extern int  g_time_hour;    // 0..24, -1 = OFF
extern int  g_time_minute;  // 0..59

void opt_time_init(const char* ini_path);
void opt_time_set_hwnd_and_start(HWND hwnd);
void opt_time_apply_hour(int hour);   // -1 = OFF, 0..24 = heure forcée
void opt_time_refresh_now();          // met à jour g_time_hour/minute pour affichage
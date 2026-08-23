#pragma once
#include <windows.h>

extern int g_speed_walk_value;  // 1-8, défaut=4
extern int g_speed_run_value;   // 1-8, défaut=5
extern int g_speed_surf_value;  // 1-8, défaut=5
extern int g_speed_bike_value;  // 1-8, défaut=6 

void opt_speed_init(const char* ini_path);
void opt_speed_set_hwnd_and_start(HWND hwnd);

void opt_speed_apply_walk(int value);
void opt_speed_apply_run(int value);
void opt_speed_apply_surf(int value);
void opt_speed_apply_bike(int value);
void opt_speed_reset_defaults();

#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern bool g_game_speed_enabled;
extern int  g_game_speed_factor;

void opt_gamespeed_init(const char* ini_path);
void opt_gamespeed_set_hwnd_and_start(HWND hwnd);
void opt_gamespeed_toggle(bool enabled);
void opt_gamespeed_apply(int factor);
int  opt_gamespeed_get_hold_key();
void opt_gamespeed_set_hold_key(int virtual_key);
int  opt_gamespeed_get_hold_gamepad();
void opt_gamespeed_set_hold_gamepad(int binding);
void opt_gamespeed_get_hold_key_name(char* buffer, int capacity);
